#include "tcpkernel.h"
#include "../config/ConfigParser.h"
#include <QCoreApplication>
#include <QStringList>
#include <QStandardPaths>   // 缺省存储根按平台解析（不写死 Windows 路径）
#include <algorithm>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <random>   // 分享码生成所用的 CSPRNG 随机数源
#include <set>      // F9-2：对 AI 与 LIKE 搜索结果去重
#include <map>      // 本地检索结果 fileId → 文件元数据
#include <vector>
#include <cstdio>

namespace {

/// 本地内容检索的召回上限（倒排召回 + 余弦排序后的 TopN；最终结果再按协议 MAXSIZE 截断）。
/// 30 = 远大于单次响应可容纳条数，保证"用户可见文件过滤"后仍有足够候选。
const size_t kLocalSearchTopN = 30;

/// 本地检索结果对应的文件元数据（SQL 过滤到"该用户可见"后填充）。
struct SearchFileMeta {
    int64_t     fileId;
    std::string name;
    int64_t     size;
    std::string sha256;
};

/// 去重后追加标签（保序：先到的标签在前）。
void pushUniqueTag(std::vector<std::string>& tags, const std::string& tag)
{
    if (tag.empty())
        return;
    for (size_t i = 0; i < tags.size(); ++i) {
        if (tags[i] == tag)
            return;
    }
    tags.push_back(tag);
}

/// 本地标签（规则 + 词典）—— 无任何命中时用原始扩展名兜底，保证非空
/// （设计 5.4：最差情况"标签 = 扩展名"，绝不让标签为空）。
std::vector<std::string> localTagsWithFallback(const LocalTagEngine& engine,
                                              const std::string& text,
                                              const std::string& fileName)
{
    std::vector<std::string> tags = engine.tagFile(text, fileName);
    if (tags.empty()) {
        const size_t dot = fileName.find_last_of('.');
        pushUniqueTag(tags, (dot != std::string::npos && dot + 1 < fileName.size())
                                ? fileName.substr(dot + 1)
                                : std::string("unknown"));
    }
    return tags;
}

} // namespace

Ikernel *tcpkernel::m_kernel=new tcpkernel;

// m_lstFileInfo 的线程安全辅助函数（由 IOCP worker 线程与 DbWorker 线程访问）
STRU_FILEINFO* tcpkernel::findFileInfoLocked(int64_t fileId) {
    std::lock_guard<std::mutex> lock(m_fileInfoMutex);
    for (auto* p : m_lstFileInfo) {
        if (p->m_fileid == fileId) return p;
    }
    return nullptr;
}

void tcpkernel::addFileInfoLocked(STRU_FILEINFO* p) {
    std::lock_guard<std::mutex> lock(m_fileInfoMutex);
    m_lstFileInfo.push_back(p);
}

tcpkernel::tcpkernel() {
    m_server=new IocpServer();
    m_sql=new MySqlWrapper();
    m_storage=nullptr;
    m_bloomFilter=nullptr;
    m_uploadState=nullptr;
    m_nodeMgr=new NodeManager();
    m_httpServer=new HttpServer();
    m_aiPreview = new AIFilePreview();
    m_aiSearch = new AISearchSvc(m_sql);
    m_aiTag = new AITagService();
    // 本地检索引擎：检索层持有内存倒排索引的非拥有指针（索引随 tcpkernel 生命周期存在）
    m_localSearch = new LocalSearchEngine(&m_localIndex);
    m_httpPort = 0;  // 在 boolopen() 中从 server.conf 读取
    m_szSystemPath[0] = '\0';  // 在 boolopen() 中从 server.conf 读取
}

tcpkernel::~tcpkernel()
{
    m_dbWorker.stop();
    m_replWorker.stop();  // 必须在 delete m_nodeMgr 之前停止——worker 线程会使用 NodeManager

    delete m_server;
    m_server=NULL;

    delete m_sql;
    m_sql=NULL;

    delete m_storage;
    m_storage=NULL;

    delete m_bloomFilter;
    m_bloomFilter=NULL;

    delete m_uploadState;
    m_uploadState=NULL;

    delete m_nodeMgr;
    m_nodeMgr=NULL;

    delete m_httpServer;
    m_httpServer=NULL;

    delete m_aiPreview; m_aiPreview = nullptr;
    delete m_aiSearch;   m_aiSearch = nullptr;
    delete m_aiTag;      m_aiTag = nullptr;
    delete m_localSearch; m_localSearch = nullptr;   // 非拥有 m_localIndex（成员，自动析构）
}

bool tcpkernel::boolopen()
{
    // ===================================================================
    // 从 server.conf 读取全部配置（配置驱动，无硬编码）
    // 配置路径：--config <path> 参数，默认使用 "server.conf"
    // ===================================================================
    std::string configPath = "server.conf";
    {
        QStringList args = QCoreApplication::arguments();
        for (int i = 1; i < args.size(); ++i) {
            if (args[i] == "--config" && i + 1 < args.size()) {
                configPath = args[i + 1].toStdString();
                break;
            }
        }
    }
    std::ifstream configFile(configPath);
    if (!configFile) {
        printf("Config file not found: %s\n", configPath.c_str());
        return false;
    }
    std::stringstream configSS;
    configSS << configFile.rdbuf();
    std::string configJson = configSS.str();
    configFile.close();

    std::string listenIP    = jsonGetString(configJson, "listen_ip");
    int         listenPort  = jsonGetInt(configJson, "listen_port");
    int         workerCount = jsonGetInt(configJson, "worker_threads");
    std::string storagePath = jsonGetString(configJson, "storage_path");
    std::string mysqlHost   = jsonGetString(configJson, "mysql_host");
    std::string mysqlUser   = jsonGetString(configJson, "mysql_user");
    std::string mysqlPass   = jsonGetString(configJson, "mysql_password");
    std::string mysqlDb     = jsonGetString(configJson, "mysql_database");
    int         httpPort    = jsonGetInt(configJson, "http_port");
    // 流媒体访问控制与超时（可选，缺省用代码默认值）
    //   stream_token_ttl     ：签发性凭证有效期（秒），默认 60
    //   stream_session_idle  ：播放会话空闲有效期（秒），默认 3600
    //   http_send_timeout_ms ：客户端发送/接收超时（毫秒），默认 10000
    int         streamTokenTtl    = jsonGetInt(configJson, "stream_token_ttl");
    int         streamSessionIdle = jsonGetInt(configJson, "stream_session_idle");
    int         httpSendTimeoutMs = jsonGetInt(configJson, "http_send_timeout_ms");

    // 为缺失的配置项应用默认值
    if (listenIP.empty())    listenIP    = "127.0.0.1";
    if (listenPort == 0)     listenPort  = 8899;
    if (workerCount == 0)    workerCount = 4;
    if (storagePath.empty()) {
        // 缺省存储根：交给 Qt 按平台解析，避免在业务代码里写死 Windows 路径
        //   Windows: %APPDATA% 下的应用数据目录 + /storage/
        //   Linux  : ~/.local/share 下的应用数据目录 + /storage/
        // 正常部署都由 server.conf 的 storage_path 指定，这里只是兜底。
        storagePath = (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                       + "/storage/").toStdString();
    }
    if (mysqlHost.empty())   mysqlHost   = "localhost";
    if (mysqlUser.empty())   mysqlUser   = "root";
    if (mysqlDb.empty())     mysqlDb     = "new_schema15";
    if (httpPort == 0)       httpPort    = 8900;
    if (streamTokenTtl <= 0)    streamTokenTtl    = 60;
    if (streamSessionIdle <= 0) streamSessionIdle = 3600;
    if (httpSendTimeoutMs <= 0) httpSendTimeoutMs = 10000;

    // 保存到成员变量中，供各处理函数使用
    m_httpPort = httpPort;
    strcpy(m_szSystemPath, storagePath.c_str());

    printf("Config loaded: listen=%s:%d workers=%d storage=%s mysql=%s@%s/%s http=%d\n",
           listenIP.c_str(), listenPort, workerCount, storagePath.c_str(),
           mysqlUser.c_str(), mysqlHost.c_str(), mysqlDb.c_str(), httpPort);

    // --- AI 配置（可选） ---
    {
        std::string aiSection = jsonGetObject(configJson, "ai");
        if (!aiSection.empty()) {
            std::string aiProvider = jsonGetString(aiSection, "provider");
            std::string aiBaseUrl = jsonGetString(aiSection, "base_url");
            std::string aiChatModel = jsonGetString(aiSection, "chat_model");
            std::string aiEmbedModel = jsonGetString(aiSection, "embedding_model");
            // server.conf 中可选的 api_key 字段（环境变量仍然优先）
            std::string aiApiKey = jsonGetString(aiSection, "api_key");

            // 密钥优先级不变：优先环境变量，其次 server.conf 中的可选 api_key
            const char* envKey = nullptr;
            if (aiProvider == "siliconflow" || aiProvider.empty()) {
                envKey = std::getenv("SILICONFLOW_API_KEY");
                if (!envKey) envKey = std::getenv("OPENAI_API_KEY");  // 回退
            } else if (aiProvider == "openai") {
                envKey = std::getenv("OPENAI_API_KEY");
            }
            std::string apiKey = (envKey && strlen(envKey) > 0) ? std::string(envKey) : aiApiKey;

            // Ollama 是本地服务：无需 API Key；其他服务需从环境变量或配置获取 Key
            bool isOllama = (aiProvider == "ollama");
            if (isOllama || !apiKey.empty()) {
                AiConfig aiCfg;
                aiCfg.provider = aiProvider.empty() ? "siliconflow" : aiProvider;
                aiCfg.apiKey = apiKey;
                aiCfg.baseUrl = aiBaseUrl.empty() ? "https://api.siliconflow.cn/v1" : aiBaseUrl;
                aiCfg.chatModel = aiChatModel.empty() ? "deepseek-ai/DeepSeek-V3" : aiChatModel;
                aiCfg.embeddingModel = aiEmbedModel.empty() ? "BAAI/bge-m3" : aiEmbedModel;

                // Ollama 默认配置（原生 /api/chat + /api/embed 协议）
                if (isOllama) {
                    if (aiBaseUrl.empty())      aiCfg.baseUrl = "http://localhost:11434";
                    if (aiChatModel.empty())    aiCfg.chatModel = "qwen2.5:7b";
                    if (aiEmbedModel.empty())   aiCfg.embeddingModel = "bge-m3";
                }

                APIBridge::instance()->initFromConfig(aiCfg);
            } else {
                printf("WARNING: AI config present in server.conf but no API key in env or config — AI disabled\n");
            }
        }
        // 若配置中没有 "ai" 段，APIBridge 已在构造函数中尝试过环境变量
    }

    // --- IocpServer：启动前设置回调 ---
    m_server->setDataCallback([this](SOCKET sock, const char* data, int len) {
        this->dealData(sock, data, len);
    });
    m_server->setDisconnectCallback([this](SOCKET sock) {
        (void)sock;  // TODO：维护 socket→用户映射，实现按连接清理
        // 关键修复：任一客户端断开时不得清空全部上传。
        // 每个条目必须与具体 socket 绑定，以便按作用域清理。
    });

    if (!m_server->start(listenIP.c_str(), listenPort, workerCount)) {
        printf("IocpServer start failed\n");
        return false;
    }

    // --- MySqlWrapper：连接 ---
    if (!m_sql->connect(mysqlHost.c_str(), mysqlUser.c_str(), mysqlPass.c_str(), mysqlDb.c_str())) {
        printf("MySQL connect failed\n");
        return false;
    }

    // --- Bloom Filter 预热（同步执行，在 DbWorker 启动之前） ---
    m_bloomFilter = new BloomFilter();
    warmupBloomFilter();

    // --- L2 稀疏指纹预热（将已知指纹加载到内存） ---
    {
        std::list<std::string> sparseList;
        m_sql->query("SELECT f_sparse_sha256 FROM files WHERE f_sparse_sha256 != ''",
                     {}, 1, sparseList);
        for (const auto& fp : sparseList) {
            if (!fp.empty()) m_sparseFingerprints.insert(fp);
        }
        printf("Sparse fingerprints warmed up: %zu loaded\n", m_sparseFingerprints.size());
    }

    // --- DbWorker：启动异步数据库线程（预热后再启动，避免连接冲突） ---
    // 注意顺序约束：标签词典必须在 DbWorker 启动之前加载完成。
    //   词典加载会在主线程上替换 m_localTag 内部的 std::map；而 aitagrq / 上传完成路径
    //   会在 DbWorker 线程上遍历该 map（localTagsWithFallback → tagFile）。若词典加载
    //   晚于 DbWorker 启动（原实现放在 boolopen 末尾），启动期到达的请求就可能读到
    //   正在被 swap 的 map → 撕裂指针 → 访问违例（"内存不能为 read"）。
    //   放在此处：DbWorker 尚未启动（请求只是入队不执行）、客户端也刚连上，窗口为零。
    loadLocalTagDict();
    m_dbWorker.start();

    // --- 集群 NodeManager ---
    if (!m_nodeMgr->init(configPath.c_str())) {
        printf("NodeManager: init failed, running in standalone mode\n");
        // 非致命错误——以单机模式继续运行
    }

    // --- ReplicationWorker：跨节点异步块复制 ---
    m_replWorker.start(m_nodeMgr);

    // --- FileStorage ---
    m_storage = new FileStorage();
    if (!m_storage->init(storagePath.c_str())) {
        printf("FileStorage init failed\n");
        return false;
    }

    // --- SQLite 上传状态（用于断点续传） ---
    m_uploadState = new SqliteState();
    {
        std::string uploadDbPath = storagePath + "upload_state.db";
        if (!m_uploadState->open(uploadDbPath.c_str())) {
            printf("SqliteState: failed to open database\n");
            // 非致命错误
        }
    }

    // 恢复未完成的上传
    recoverUploads();

    // --- HTTP 流媒体服务器（视频/音频/图片在线播放） ---
    // 先装配访问控制与会话参数，再启动（start() 之后不再改动）
    m_httpServer->access().setTokenTtlSeconds(streamTokenTtl);
    m_httpServer->access().setSessionIdleSeconds(streamSessionIdle);
    m_httpServer->setSendTimeoutMs(httpSendTimeoutMs);
    m_httpServer->setRecvTimeoutMs(httpSendTimeoutMs);

    printf("[HttpServer] stream access: token_ttl=%ds session_idle=%ds send_timeout=%dms\n",
           streamTokenTtl, streamSessionIdle, httpSendTimeoutMs);

    m_httpServer->start(httpPort, m_storage);

    // --- 初始化 APIBridge（从环境变量读取 OPENAI_API_KEY） ---
    APIBridge::instance();  // 单例初始化——记录 AI 是否启用

    return true;
}

// ============================================================================
// 本地检索引擎——索引维护与词典部署（Task 8 / Task 9）
// ============================================================================

// 内容级增量索引：上传完成 / 预览读取到内容时调用。
//   - 只索引"可提取文本"的文件（TxtTextExtractor::canHandle）：二进制/未支持格式不入索引，
//     这类文件的检索由文件名 LIKE 兜底（设计 §5.2 解析器可插拔 + §5.4 保证非空）；
//   - addDocument 幂等（内部先移除旧项再重建），因此重复索引同一文件安全；
//   - 仅由 DbWorker 线程调用（m_localIndex 不加锁）。
void tcpkernel::indexLocalContent(int64_t fileId, const std::string& fileName, const std::string& rawContent)
{
    if (fileId <= 0 || rawContent.empty())
        return;

    TxtTextExtractor extractor;
    if (!extractor.canHandle(fileName))
        return;

    const std::string text = extractor.extract(rawContent);   // 剥 BOM + 截断到 64KB
    if (text.empty())
        return;

    m_localIndex.addDocument(fileId, text);
}

// 词典部署路径策略（Task 9 遗留决策）——按优先级尝试，全部失败只告警，不崩溃：
//   ① exe 同目录 / 当前工作目录的 tag_dict.json（部署形态：随 server 一起发布）
//   ② 编译期源码绝对路径 LOCAL_TAG_DICT_PATH（开发形态：从任意工作目录启动都能命中）
//   ③ exe 目录下的源码相对回退 ../ai/local/tag_dict.json（在 0323server/release/ 内直接运行）
// 全部失败 → 退化为纯扩展名规则（tagFile 对已知扩展名仍返回非空标签）。
void tcpkernel::loadLocalTagDict()
{
    const QString appDir = QCoreApplication::applicationDirPath();

    std::vector<std::string> candidates;
    candidates.push_back((appDir + "/tag_dict.json").toStdString());
    candidates.push_back("tag_dict.json");
#ifdef LOCAL_TAG_DICT_PATH
    candidates.push_back(std::string(LOCAL_TAG_DICT_PATH));
#endif
    candidates.push_back((appDir + "/../ai/local/tag_dict.json").toStdString());

    for (size_t i = 0; i < candidates.size(); ++i) {
        if (m_localTag.loadDict(candidates[i])) {
            printf("[LocalTagEngine] tag dictionary loaded: %s (%zu tags)\n",
                   candidates[i].c_str(), m_localTag.dictSize());
            fflush(stdout);   // 启动诊断：重定向到日志文件时立即可见（否则要等进程退出才落盘）
            return;
        }
    }

    fprintf(stderr, "[LocalTagEngine] WARNING: tag_dict.json not found (tried %zu paths) — "
                    "degraded to extension-only rules (tags remain non-empty)\n",
            candidates.size());
    fflush(stderr);
}

void tcpkernel::close()
{
    m_httpServer->stop();
    m_dbWorker.stop();
    m_replWorker.stop();
    m_sql->disconnect();
    m_server->stop();
}

void tcpkernel::recoverUploads()
{
    if (!m_uploadState) return;
    auto unfinished = m_uploadState->getUnfinishedUploads();
    if (unfinished.empty()) return;

    printf("Recovering %zu unfinished uploads...\n", unfinished.size());
    for (auto& s : unfinished) {
        // 断点续传数据直接落在 FileStorage(blocks.dat) 中（F4-5 后不再依赖临时文件），
        // 因此以"该文件是否已有落盘块"判断能否续传，而不是检查临时文件是否存在。
        bool hasBlocks = m_storage && !m_storage->getFileBlocks(s.fileId).empty();
        if (!hasBlocks) {
            m_uploadState->setState(s.fileHash, s.userId, "abandoned");
            if (m_storage && s.fileId > 0) {
                m_storage->deleteFile(s.fileId);  // 无块可续：清残留（幂等）
                printf("  Abandoned (no stored blocks), cleaned fileId=%lld\n",
                       (long long)s.fileId);
            } else {
                printf("  Abandoned (no stored blocks) for hash %s\n", s.fileHash.c_str());
            }
            continue;
        }
        // 已落盘的块保留：状态保持 uploading，等待客户端重新上传时按 last_offset 续传
        printf("  Resumable: user=%lld file=%lld offset=%lld/%lld\n",
               s.userId, s.fileId, s.lastOffset, s.fileSize);
    }
}

bool tcpkernel::dealData(SOCKET sock, const char *szbuf, int nlen)
{
    // 关键：将整个分发过程包裹在 try-catch 中，防止畸形数据包
    // 导致服务器崩溃。BinaryStream 反序列化在数据截断或损坏时
    // 抛出 std::runtime_error；若无此捕获，IOCP worker 线程中的
    // 未处理异常将触发 std::terminate() → 整个服务器进程退出。
    try {
        switch(*szbuf)
        {
        case _default_protocol_register_rq:
            registerrq(sock, szbuf, nlen);
            break;
        case _default_protocol_login_rq:
            loginrq(sock, szbuf, nlen);
            break;
        case _default_protocol_getfilelist_rq:
            getfilelistrq(sock, szbuf, nlen);
            break;
        case _default_protocol_uploadfileinfo_rq:
            uploadfileinforq(sock, szbuf, nlen);
            break;
        case _default_protocol_uploadfileblock_rq:
            uploadfileblockrq(sock, szbuf, nlen);
            break;
        case _default_protocol_downloadfileinfo_rq:
            downloadfileinforq(sock, szbuf, nlen);
            break;
        case _default_protocol_downloadfileblock_rq:
            downloadfileblockrq(sock, szbuf, nlen);
            break;
        case _default_protocol_deletefile_rq:
            deletefilerq(sock, szbuf, nlen);
            break;
        case _default_protocol_sharefile_rq:
            sharefilerq(sock, szbuf, nlen);
            break;
        case _default_protocol_deleteshare_rq:
            deletesharerq(sock, szbuf, nlen);
            break;
        case _default_protocol_getfile_rq:
            getfilerq(sock, szbuf, nlen);
            break;
        case _default_protocol_searchfile_rq:
        {
            // 已弃用：旧的文件名搜索已被 AI Search（#26）取代
            // 转发给 aisearch 处理函数以保持向后兼容
            STRU_SEARCHFILERQ oldReq = ProtocolFactory::deserializeSearchFileRQ(szbuf + 1, nlen - 1);
            STRU_AISEARCHRQ newReq = {};
            newReq.m_userId = oldReq.m_userId;
            strncpy(newReq.m_szQuery, oldReq.m_szSearchKey, MAXSIZE - 1);
            auto pkt = ProtocolFactory::serializeAISearchRQ(newReq);
            aisearchrq(sock, (const char*)pkt.data(), (int)pkt.size());
            break;
        }
        case _default_protocol_replicateblock_rq:
            replicateblockrq(sock, szbuf, nlen);
            break;
        case _default_protocol_sparsecheck_rq:
            sparsecheckrq(sock, szbuf, nlen);
            break;
        case _default_protocol_streamtoken_rq:
            streamtokenrq(sock, szbuf, nlen);
            break;
        case _default_protocol_aipreview_rq:
            aipreviewrq(sock, szbuf, nlen);
            break;
        case _default_protocol_aisearch_rq:
            aisearchrq(sock, szbuf, nlen);
            break;
        case _default_protocol_aitag_rq:
            aitagrq(sock, szbuf, nlen);
            break;
        }
    } catch (const std::exception& e) {
        // 记录错误并断开违规客户端。
        // IOCP worker 线程继续运行——服务器保持在线。
        fprintf(stderr, "[DEALDATA] Exception from client on socket %lld: %s\n",
                (long long)sock, e.what());
        // 断开发送错误数据包的客户端
        m_server->disconnectClient(sock);
        return false;
    } catch (...) {
        fprintf(stderr, "[DEALDATA] Unknown exception from client on socket %lld\n",
                (long long)sock);
        m_server->disconnectClient(sock);
        return false;
    }

    return true;
}

// ============================================================================
// 1. 注册 REGISTER — 参数化查询 + SHA-256 密码哈希 + 异步数据库
// ============================================================================
void tcpkernel::registerrq(SOCKET sock, const char *szbuf, int nlen)
{
    STRU_REGISTERRQ req = ProtocolFactory::deserializeRegisterRQ(szbuf + 1, nlen - 1);

    // F2-1 修复：移除 m_szPassword 明文密码字段；仅发送 SHA-256
    std::string hashedPassword(req.m_szPasswordSHA256);

    m_dbWorker.enqueue([this, req, hashedPassword, sock]() {
        STRU_REGISTERRS rs;
        rs.m_szResult = _register_err;

        bool ok = m_sql->execute(
            "INSERT INTO user(u_name,u_password,u_tel) VALUES(?,?,?)",
            {std::string(req.m_szName), hashedPassword, static_cast<int64_t>(req.m_tel)});

        if (ok) {
            std::list<std::string> lst;
            m_sql->query("SELECT u_id FROM user WHERE u_name=?",
                {std::string(req.m_szName)}, 1, lst);
            if (!lst.empty()) {
                rs.m_szResult = _register_success;
                std::string path = std::string(m_szSystemPath) + lst.front();
                // F1-4 修复：检查 CreateDirectoryA 返回值，失败时告警
                if (!CreateDirectoryA(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
                    fprintf(stderr, "[REGISTER] WARNING: Failed to create user dir: %s (err=%lu)\n",
                            path.c_str(), GetLastError());
                }
            }
        }

        auto packet = ProtocolFactory::serializeRegisterRS(rs);
        m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
    });
}

// ============================================================================
// 2. 登录 LOGIN — 参数化查询 + SHA-256 比对（明文回退）+ 异步数据库
// ============================================================================
void tcpkernel::loginrq(SOCKET sock, const char *szbuf, int nlen)
{
    STRU_LOGINRQ req = ProtocolFactory::deserializeLoginRQ(szbuf + 1, nlen - 1);

    // F2-1 修复：移除 m_szPassword 明文密码字段；仅发送 SHA-256
    std::string hashedInput(req.m_szPasswordSHA256);

    m_dbWorker.enqueue([this, req, hashedInput, sock]() {
        STRU_LOGINRS sl;
        // F2-2 修复：统一错误提示——不泄露用户是否存在或密码是否正确
        sl.m_szResult = _login_invalid;
        sl.m_userId = 0;

        std::list<std::string> lst;
        m_sql->query("SELECT u_id,u_password FROM user WHERE u_name=?",
            {std::string(req.m_szName)}, 2, lst);

        if (lst.size() >= 2) {
            std::string strUserId = lst.front(); lst.pop_front();
            std::string strPassword = lst.front(); lst.pop_front();

            if (hashedInput == strPassword) {
                sl.m_szResult = _login_success;
                sl.m_userId = atoll(strUserId.c_str());
            }
        }

        auto packet = ProtocolFactory::serializeLoginRS(sl);
        m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
    });
}

// ============================================================================
// 3. 获取文件列表 GET FILE LIST — Bug #6 修复：改用 JOIN user_file 而非 files.u_id
// ============================================================================
void tcpkernel::getfilelistrq(SOCKET sock, const char *szbuf, int nlen)
{
    STRU_GETFILELISTRQ req = ProtocolFactory::deserializeGetFileListRQ(szbuf + 1, nlen - 1);

    m_dbWorker.enqueue([this, req, sock]() {
        std::list<std::string> lst;
        STRU_GETFILELISTRS sg;
        sg.m_nFileNum = 0;
        int i = 0;
        bool sentAny = false;

        // Bug #6 修复：JOIN user_file 获取该用户拥有的文件
        // 旧（错误）：SELECT ... FROM files WHERE u_id=%lld
        // 新（正确）：JOIN user_file ON f.f_id = uf.f_id WHERE uf.u_id = ?
        // F3-5 修复：添加 LIMIT 防止无界内存消耗
        m_sql->query(
            "SELECT f.f_name, f.f_size, f.f_uploadtime, f.f_id "
            "FROM files f JOIN user_file uf ON f.f_id = uf.f_id "
            "WHERE uf.u_id = ? "
            "ORDER BY f.f_uploadtime DESC LIMIT 500",
            {static_cast<int64_t>(req.m_userId)}, 4, lst);

        while (lst.size() > 0) {
            std::string strFileName = lst.front(); lst.pop_front();
            std::string strFileSize = lst.front(); lst.pop_front();
            std::string strFileUploadTime = lst.front(); lst.pop_front();
            std::string strFileId = lst.front(); lst.pop_front();

            strcpy(sg.m_aryFileInfo[i].m_szFileName, strFileName.c_str());
            strcpy(sg.m_aryFileInfo[i].m_szFileUploadTime, strFileUploadTime.c_str());
            sg.m_aryFileInfo[i].m_filesize = atoll(strFileSize.c_str());
            sg.m_aryFileInfo[i].m_fileID = atoll(strFileId.c_str());
            ++i;
            if (i == MAXSIZE || lst.size() == 0) {
                sg.m_nFileNum = i;
                auto packet = ProtocolFactory::serializeGetFileListRS(sg);
                m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
                ZeroMemory(sg.m_aryFileInfo, sizeof(sg.m_aryFileInfo));
                i = 0;
                sentAny = true;
            }
        }
        // 关键修复：用户没有文件时发送空响应
        if (!sentAny) {
            sg.m_nFileNum = 0;
            auto packet = ProtocolFactory::serializeGetFileListRS(sg);
            m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
        }
    });
}

// ============================================================================
// 4. 上传文件信息 UPLOAD FILE INFO — SHA-256 断点续传（本地）+ MD5 校验（异步数据库）
// ============================================================================
void tcpkernel::uploadfileinforq(SOCKET sock, const char *szbuf, int nlen)
{
    STRU_UPLOADFILEINFORQ req = ProtocolFactory::deserializeUploadFileInfoRQ(szbuf + 1, nlen - 1);

    // ===================================================================
    // 第一层：基于 SHA-256 的断点续传/秒传检查（SQLite，本地）
    // ===================================================================
    std::string fileHash(req.m_szFileSHA256);
    if (!fileHash.empty() && m_uploadState) {
        UploadState* state = m_uploadState->getState(fileHash, req.m_userId);
        if (state) {
            if (state->state == "uploading") {
                STRU_FILEINFO *pResume = new STRU_FILEINFO;
                pResume->m_fileid = state->fileId;
                pResume->m_filepos = state->lastOffset;
                pResume->m_filesize = state->fileSize;
                pResume->m_userid = req.m_userId;
                strcpy(pResume->m_szFileSHA256, fileHash.c_str());
                // 续传数据直接写入 FileStorage(blocks.dat)，不再依赖临时文件（F4-5 口径）
                pResume->m_pfile = nullptr;
                pResume->m_resumed = true;   // 续传标记：完成校验时按已存块重算完整 SHA-256
                addFileInfoLocked(pResume);

                STRU_UPLOADFILEINFORS rs;
                strcpy(rs.m_szFileName, req.m_fileInfo.m_szFileName);
                strcpy(rs.m_szFileSHA256, req.m_szFileSHA256);
                rs.m_fileID = state->fileId;
                rs.m_pos = state->lastOffset;
                rs.m_szResult = _uploadfile_continue;

                auto packet = ProtocolFactory::serializeUploadFileInfoRS(rs);
                m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
                delete state;
                return;
            }
            if (state->state == "committed") {
                STRU_UPLOADFILEINFORS rs;
                strcpy(rs.m_szFileName, req.m_fileInfo.m_szFileName);
                strcpy(rs.m_szFileSHA256, req.m_szFileSHA256);
                rs.m_fileID = state->fileId;
                rs.m_pos = state->fileSize;
                rs.m_szResult = _uploadfile_isuploaded;
                auto packet = ProtocolFactory::serializeUploadFileInfoRS(rs);
                m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
                delete state;
                return;
            }
            delete state;
        }
    }

    // ===================================================================
    // 第二层：Bloom Filter 预检（三层漏斗中的 L3）
    // 避免全新文件产生不必要的数据库往返。
    // 若 Bloom 判定"数据库中肯定不存在"，则完全跳过 MySQL 秒传
    // 查询，直接进入普通上传路径。
    // ===================================================================
    bool bloomHit = false;
    if (m_bloomFilter && !fileHash.empty()) {
        bloomHit = m_bloomFilter->mightContain(fileHash);
        if (!bloomHit) {
            // Bloom Filter 判定"数据库中肯定不存在"——跳过 MySQL 秒传检查，
            // 直接进入普通上传。这是新文件的约 1μs 快速路径。
            qDebug() << "[Bloom L3] MISS — skipping MySQL flash check, proceeding to normal upload";
        } else {
            qDebug() << "[Bloom L3] HIT — will verify with MySQL query";
        }
    }

    // ===================================================================
    // 第三层：MySQL 中的 MD5 校验（通过 DbWorker 异步执行）
    // ===================================================================
    // 在 lambda 中按值捕获所需的一切
    std::string sha256(req.m_szFileSHA256);
    std::string fname(req.m_fileInfo.m_szFileName);
    int64_t userId = req.m_userId;
    int64_t fileSize = req.m_fileInfo.m_filesize;
    std::string uploadTime(req.m_fileInfo.m_szFileUploadTime);

    m_dbWorker.enqueue([this, sock, req, fileHash, sha256, fname,
                        userId, fileSize, uploadTime, bloomHit]() {
        std::list<std::string> lst;
        STRU_UPLOADFILEINFORS su;
        strcpy(su.m_szFileName, fname.c_str());
        strcpy(su.m_szFileSHA256, fileHash.c_str());
        su.m_pos = 0;
        su.m_fileID = 0;

        // 仅当 Bloom Filter 命中或不可用时才执行 MySQL 秒传检查。
        // 若 Bloom 判定"肯定不存在"，则跳过数据库查询，直接进入普通上传。
        if (bloomHit) {
        // Bug #6 修复：JOIN user_file 检查文件所有权（files 表没有 u_id）
        // F5-4 修复：仅按哈希匹配——内容相同、文件名不同也能秒传
        m_sql->query(
            "SELECT uf.u_id, f.f_id FROM files f "
            "JOIN user_file uf ON f.f_id = uf.f_id "
            "WHERE f.f_sha256=? LIMIT 1",
            {sha256}, 2, lst);

        if (lst.size() >= 2) {
            std::string strUserId = lst.front(); lst.pop_front();
            std::string strFileId = lst.front(); lst.pop_front();
            long long existingUserId = atoll(strUserId.c_str());
            long long fileId = atoll(strFileId.c_str());

            if (existingUserId == userId) {
                // 同一用户——已上传（或断点续传）
                su.m_szResult = _uploadfile_isuploaded;
            } else {
                // 不同用户——秒传（flash upload）
                su.m_szResult = _uploadfile_flash;

                // 引用计数加一
                m_sql->execute(
                    "UPDATE files SET fcount = fcount + 1 WHERE files.f_sha256=?",
                    {sha256});

                // 创建用户-文件映射
                m_sql->execute(
                    "INSERT INTO user_file(u_id,f_id) VALUES(?,?)",
                    {userId, fileId});

                // 在 SQLite 中标记为已提交，供后续查询使用
                if (m_uploadState && !fileHash.empty()) {
                    UploadState us;
                    us.fileId = fileId;
                    us.userId = userId;
                    us.fileSize = fileSize;
                    us.totalBlocks = 1;
                    us.completedBlocks = 1;
                    us.lastOffset = fileSize;
                    us.tempPath = "";
                    us.fileHash = fileHash;
                    us.state = "committed";
                    m_uploadState->createState(us);
                }
            }

            // 发送秒传/已上传响应
            auto packet = ProtocolFactory::serializeUploadFileInfoRS(su);
            m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
            return;
        }
        } // if (bloomHit) 结束——Bloom 未命中则落入普通上传流程

        // ===============================================================
        // 文件不存在（或 Bloom 未命中）——普通上传
        // ===============================================================
        su.m_szResult = _uploadfile_normal;

        // 构造文件路径（F4-3 修复：userId 与文件名之间加路径分隔符）
        char szfilepath[260] = {0};
        snprintf(szfilepath, sizeof(szfilepath), "%s%lld\\%s", m_szSystemPath, userId, fname.c_str());
        // F4-5 修复：移除无效的 fopen——数据写入 FileStorage，而非这个空文件
        // 确保用户目录存在
        std::string userDir = std::string(m_szSystemPath) + std::to_string(userId);
        CreateDirectoryA(userDir.c_str(), nullptr);

        // 将文件元数据插入数据库（参数化）
        long long fileid = 0;
        bool inserted = m_sql->execute(
            "INSERT INTO files(f_name,f_size,f_uploadtime,f_sha256,f_path) VALUES(?,?,?,?,?)",
            {fname, fileSize, uploadTime, sha256, std::string(szfilepath)});

        if (inserted) {
            // 获取分配的文件 ID
            std::list<std::string> idLst;
            m_sql->query(
                "SELECT f_id FROM files WHERE f_sha256=? AND f_name=?",
                {sha256, fname}, 1, idLst);
            if (!idLst.empty()) {
                fileid = atoll(idLst.front().c_str());
                su.m_fileID = fileid;

                // F16-2 修复：重定向检查——若该文件应存放到对等节点，
                // 则清理本地数据库记录并通知客户端重新连接。
                if (m_nodeMgr && fileid > 0 && !m_nodeMgr->isLocal(fileid)) {
                    m_sql->execute("DELETE FROM files WHERE f_id=?", {fileid});
                    fprintf(stderr, "[Upload] Redirecting fileId=%lld to peer node\n", (long long)fileid);

                    STRU_REDIRECTRS redirect;
                    strncpy(redirect.m_szRedirectIP, m_nodeMgr->getRedirectIP(fileid).c_str(), 15);
                    redirect.m_szRedirectIP[15] = '\0';
                    redirect.m_nRedirectPort = m_nodeMgr->getRedirectPort(fileid);
                    redirect.m_szResult = _redirect_permanent;
                    auto pkt = ProtocolFactory::serializeRedirectRS(redirect);
                    m_server->sendData(sock, (const char*)pkt.data(), (int)pkt.size());
                    return;  // 不再继续——客户端将向正确的节点重新上传
                }

                // 创建用户-文件映射
                m_sql->execute(
                    "INSERT INTO user_file(u_id,f_id) VALUES(?,?)",
                    {userId, fileid});
            }
        }

        // 创建 STRU_FILEINFO 以跟踪上传进度
        STRU_FILEINFO *p = new STRU_FILEINFO;
        p->m_fileid = fileid;
        p->m_filepos = 0;
        p->m_filesize = fileSize;
        p->m_pfile = nullptr;  // F4-5：不创建空文件——数据写入 FileStorage
        p->m_userid = userId;
        strcpy(p->m_szFileSHA256, fileHash.c_str());
        addFileInfoLocked(p);

        // 创建 SQLite 上传状态，用于断点续传跟踪
        if (m_uploadState && !fileHash.empty()) {
            UploadState us;
            us.fileId = fileid;
            us.userId = userId;
            us.fileSize = fileSize;
            us.totalBlocks = (int)((fileSize + MAXFILECONTENT - 1) / MAXFILECONTENT);
            us.completedBlocks = 0;
            us.lastOffset = 0;
            us.tempPath = szfilepath;
            us.fileHash = fileHash;
            us.state = "uploading";
            m_uploadState->createState(us);
        }

        auto packet = ProtocolFactory::serializeUploadFileInfoRS(su);
        m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
    });
}

// ============================================================================
// 5. 上传文件块 UPLOAD FILE BLOCK — 仅 FileStorage I/O（不涉及数据库），修复 sizeof bug
// ============================================================================
void tcpkernel::uploadfileblockrq(SOCKET sock, const char *szbuf, int nlen)
{
    STRU_UPLOADFILEBLOCKRQ req = ProtocolFactory::deserializeUploadFileBlockRQ(szbuf + 1, nlen - 1);

    // 在上传列表中查找文件信息（通过互斥锁保证线程安全）
    STRU_FILEINFO* p = findFileInfoLocked(req.m_fileID);
    if (!p) return;

    // 校验块大小——防止堆缓冲区越界读取
    int actualBlockSize = (int)req.m_fileblocksize;
    if (actualBlockSize <= 0 || actualBlockSize > MAXFILECONTENT) return;

    // 通过 FileStorage 写入数据块（追加写引擎）
    int blockSeq = (int)(p->m_filepos / MAXFILECONTENT);
    int64_t offset = m_storage->writeBlock(req.m_fileID, blockSeq,
                                           req.m_szFileContent,
                                           actualBlockSize);
    if (offset >= 0) {
        p->m_filepos += req.m_fileblocksize;

        // 复制到对等节点（异步——入队后立即返回，不阻塞 IOCP）
        {
            ReplicationTask task;
            task.fileId = req.m_fileID;
            task.blockSeq = blockSeq;
            task.offset = offset;
            task.data.assign(req.m_szFileContent, req.m_szFileContent + actualBlockSize);
            task.retryCount = 0;
            task.maxRetries = 3;
            bool queued = m_replWorker.enqueue(std::move(task));
            if (!queued) {
                // F16-5 修复：复制队列满时记录告警日志
                fprintf(stderr, "[ReplicationWorker] WARNING: queue full — block seq=%d for file=%lld not replicated\n",
                        blockSeq, (long long)req.m_fileID);
            }
        }

        // F4-2 修复：随数据块到达进行增量 SHA-256 计算
        if (!p->m_sha256Active) {
            CryptoUtil::sha256Init(&p->m_sha256Ctx);
            p->m_sha256Active = true;
        }
        CryptoUtil::sha256Update(&p->m_sha256Ctx,
            reinterpret_cast<const uint8_t*>(req.m_szFileContent),
            static_cast<size_t>(actualBlockSize));

        // 每成功写入一个数据块后更新续传状态
        if (m_uploadState && strlen(p->m_szFileSHA256) > 0) {
            // F6-5 修复：completedBlocks 统计实际成功写入的数据块数
            int completedBlocks = blockSeq + 1;
            m_uploadState->updateProgress(p->m_szFileSHA256, req.m_userId,
                                          completedBlocks, p->m_filepos);
        }

        // 发送 UploadFileBlockRS 确认该数据块
        STRU_UPLOADFILEBLOCKRS blockRs;
        blockRs.m_fileID = req.m_fileID;
        blockRs.m_pos = p->m_filepos;
        blockRs.m_szResult = 1;
        auto blockPacket = ProtocolFactory::serializeUploadFileBlockRS(blockRs);
        m_server->sendData(sock, (const char*)blockPacket.data(), (int)blockPacket.size());

        // 检查上传是否完成
        if (p->m_filepos >= p->m_filesize) {
            // F6-2 修复：在 SHA-256 校验前将状态置为 "verifying"
            std::string expectedHash(p->m_szFileSHA256);
            bool hashOk = true;
            bool resumedOk = false;
            std::string resumedHash;

            if (m_uploadState && strlen(p->m_szFileSHA256) > 0) {
                m_uploadState->setState(p->m_szFileSHA256, req.m_userId, "verifying");
            }

            // F4-2 修复：校验增量 SHA-256 与期望哈希一致（续传时跳过——见下方 F4-2b）
            if (!p->m_resumed && p->m_sha256Active && !expectedHash.empty()) {
                std::string actualHash = CryptoUtil::sha256FinalHex(&p->m_sha256Ctx);
                hashOk = (actualHash == expectedHash);
            }

            // F4-2b 修复：断点/重启后续传时，增量上下文只覆盖本次会话收到的块，
            // 不能代表整个文件——改为读回 blocks.dat 中已落盘的块重算完整 SHA-256。
            if (p->m_resumed && p->m_filepos > 0) {
                bool readOk = true;
                CryptoUtil::Sha256Ctx vctx;
                CryptoUtil::sha256Init(&vctx);
                int totalBlk = (int)((p->m_filesize + MAXFILECONTENT - 1) / MAXFILECONTENT);
                for (int seq = 0; seq < totalBlk; ++seq) {
                    std::string blk = m_storage->readBlock(p->m_fileid, seq);
                    if (blk.empty()) { readOk = false; break; }
                    CryptoUtil::sha256Update(&vctx,
                        reinterpret_cast<const uint8_t*>(blk.data()), blk.size());
                }
                if (!readOk) {
                    hashOk = false;
                } else {
                    resumedOk = true;
                    resumedHash = CryptoUtil::sha256FinalHex(&vctx);
                    if (!expectedHash.empty()) {
                        hashOk = (resumedHash == expectedHash);
                    }
                }
            }

            // F4-2 修复：若 expectedHash 为空（L2"肯定是新文件"路径——
            // 客户端跳过了完整 SHA-256 计算），则根据服务端在数据块
            // 到达过程中累积的增量 SHA-256 上下文推导出真实哈希。
            std::string finalHash = expectedHash;
            if (!p->m_resumed && finalHash.empty() && p->m_sha256Active) {
                finalHash = CryptoUtil::sha256FinalHex(&p->m_sha256Ctx);
                // 回填内存中的结构体，使下游代码能看到真实哈希
                strncpy(p->m_szFileSHA256, finalHash.c_str(), sizeof(p->m_szFileSHA256) - 1);
                // 创建 SQLite 已提交记录（此前在 uploadfileinforq 中
                // 因 fileHash 为空而被跳过）
                if (m_uploadState) {
                    UploadState us;
                    us.fileId = p->m_fileid;
                    us.userId = p->m_userid;
                    us.fileSize = p->m_filesize;
                    us.totalBlocks = (int)((p->m_filesize + MAXFILECONTENT - 1) / MAXFILECONTENT);
                    us.completedBlocks = us.totalBlocks;
                    us.lastOffset = p->m_filesize;
                    us.tempPath = "";
                    us.fileHash = finalHash;
                    us.state = "committed";
                    m_uploadState->createState(us);
                }
            }

            if (p->m_resumed && resumedOk && expectedHash.empty()) {
                // 续传且客户端未提供完整哈希（L2 新文件路径）：用按块重算的哈希回填。
                finalHash = resumedHash;
                strncpy(p->m_szFileSHA256, finalHash.c_str(), sizeof(p->m_szFileSHA256) - 1);
                if (m_uploadState) {
                    UploadState us;
                    us.fileId = p->m_fileid;
                    us.userId = p->m_userid;
                    us.fileSize = p->m_filesize;
                    us.totalBlocks = (int)((p->m_filesize + MAXFILECONTENT - 1) / MAXFILECONTENT);
                    us.completedBlocks = us.totalBlocks;
                    us.lastOffset = p->m_filesize;
                    us.tempPath = "";
                    us.fileHash = finalHash;
                    us.state = "committed";
                    m_uploadState->createState(us);
                }
            }

            if (hashOk) {
                // 标记为已提交，供后续秒传检测使用
                if (m_uploadState && strlen(p->m_szFileSHA256) > 0) {
                    m_uploadState->setState(p->m_szFileSHA256, req.m_userId, "committed");
                }
                // 将新文件哈希加入 Bloom Filter，用于 L3 秒传
                if (m_bloomFilter && strlen(p->m_szFileSHA256) > 0) {
                    m_bloomFilter->insert(std::string(p->m_szFileSHA256));
                }

                // 若 L2"肯定是新文件"路径在 uploadfileinforq 中写入了
                // 空的占位哈希，此处用真实 SHA-256 回填数据库。
                if (!finalHash.empty() && finalHash != expectedHash) {
                    m_dbWorker.enqueue([this, fileId = p->m_fileid, finalHash]() {
                        m_sql->execute(
                            "UPDATE files SET f_sha256=? WHERE f_id=?",
                            {finalHash, fileId});
                    });
                }

                // --- 计算并存储稀疏指纹，用于 L2 预检 ---
                {
                    std::string head = m_storage->readBlock(p->m_fileid, 0);
                    int lastBlock = (int)((p->m_filesize - 1) / MAXFILECONTENT);
                    std::string tail = (lastBlock > 0) ? m_storage->readBlock(p->m_fileid, lastBlock) : head;
                    std::string sparseFp = CryptoUtil::sparseFingerprintFromBlocks(
                        head, tail, static_cast<uint64_t>(p->m_filesize));
                    {
                        std::lock_guard<std::mutex> lock(m_sparseFpMutex);
                        m_sparseFingerprints.insert(sparseFp);
                    }
                    // 存入数据库，便于重启后预热
                    m_dbWorker.enqueue([this, fileId = p->m_fileid, sparseFp]() {
                        m_sql->execute(
                            "UPDATE files SET f_sparse_sha256=? WHERE f_id=?",
                            {sparseFp, fileId});
                    });
                }

                // F4-5 修复：m_pfile 可能为 nullptr（数据写入 FileStorage，而非磁盘文件）
                if (p->m_pfile) {
                    fclose(p->m_pfile);
                    p->m_pfile = nullptr;
                }

                // --- 上传完成后触发本地索引/标签（主路径）与 LLM 增强（可选） ---
                int64_t completedFileId = p->m_fileid;
                int64_t completedUserId = p->m_userid;
                m_dbWorker.enqueue([this, completedFileId, completedUserId]() {
                    std::string content;
                    if (m_storage) {
                        content = m_storage->readBlock(completedFileId, 0);
                    }
                    if (!content.empty()) {
                        // 取文件名（本地索引判定可提取性 + 打标签共用同一次查询）
                        std::list<std::string> nameRows;
                        m_sql->query("SELECT f_name FROM files WHERE f_id=?",
                                     {completedFileId}, 1, nameRows);
                        const std::string fileName = nameRows.empty() ? "unknown" : nameRows.front();

                        // --- 本地内容级增量索引（Task 8）：内容来源与下方 m_aiSearch->indexFile 完全一致 ---
                        indexLocalContent(completedFileId, fileName, content);

                        // --- 本地规则/词典标签（Task 9 主路径）：无 Key 也立即有标签 ---
                        const std::vector<std::string> localTags = localTagsWithFallback(
                            m_localTag, TxtTextExtractor().extract(content), fileName);
                        m_sql->execute("DELETE FROM file_tags WHERE f_id=?", {completedFileId});
                        for (size_t i = 0; i < localTags.size(); ++i) {
                            std::string safeTag = localTags[i].size() > 99
                                                      ? localTags[i].substr(0, 99) : localTags[i];
                            m_sql->execute("INSERT IGNORE INTO file_tags(f_id, tag) VALUES(?,?)",
                                          {completedFileId, safeTag});
                            m_sql->execute(
                                "INSERT INTO tag_pool(tag, status, use_count, last_used_at) "
                                "VALUES(?,'pending',1,NOW()) ON DUPLICATE KEY UPDATE "
                                "use_count=use_count+1, last_used_at=NOW(), "
                                "status=IF(use_count >= 3, 'active', status)",
                                {safeTag});
                        }

                        // --- LLM 增强（可选；AI 关闭时不再调用，避免写入 "WeiFenLei" 占位标签）---
                        if (m_aiSearch) {
                            m_aiSearch->indexFile(completedFileId, content);
                        }
                        if (APIBridge::instance()->isEnabled() && m_aiTag) {
                            m_aiTag->tagFileAsync(completedFileId, content, fileName,
                                [this, completedFileId](const TagResult& tagResult) {
                                    if (tagResult.success) {
                                        m_dbWorker.enqueue([this, completedFileId, tagResult]() {
                                            // 与上传时落库的本地标签合并（不再 DELETE，保留规则/词典标签）
                                            for (const auto& tag : tagResult.tags) {
                                                std::string safeTag = tag.size() > 99 ? tag.substr(0, 99) : tag;
                                                m_sql->execute("INSERT IGNORE INTO file_tags(f_id, tag) VALUES(?,?)",
                                                              {completedFileId, safeTag});
                                                m_sql->execute(
                                                    "INSERT INTO tag_pool(tag, status, use_count, last_used_at) "
                                                    "VALUES(?,'pending',1,NOW()) ON DUPLICATE KEY UPDATE "
                                                    "use_count=use_count+1, last_used_at=NOW(), "
                                                    "status=IF(use_count >= 3, 'active', status)",
                                                    {safeTag});
                                            }
                                            // F15-3 修复：将新标签建议持久化到 tag_pool
                                            for (const auto& newTag : tagResult.newTagSuggestions) {
                                                std::string safeTag = newTag.size() > 99 ? newTag.substr(0, 99) : newTag;
                                                m_sql->execute(
                                                    "INSERT IGNORE INTO tag_pool(tag, status, use_count, last_used_at) "
                                                    "VALUES(?,'pending',1,NOW())",
                                                    {safeTag});
                                            }
                                        });
                                    }
                                });
                        }
                    }
                });
            } else {
                // F4-2/F6-2 修复：SHA-256 校验失败
                fprintf(stderr, "[UPLOAD] SHA-256 verification FAILED for fileId=%lld hash=%s\n",
                        (long long)p->m_fileid, expectedHash.c_str());
                if (m_uploadState && strlen(p->m_szFileSHA256) > 0) {
                    m_uploadState->setState(p->m_szFileSHA256, req.m_userId, "verification_failed");
                }
                // 清理损坏的 FileStorage 数据块
                if (m_storage) {
                    m_storage->deleteFile(p->m_fileid);
                }
                // 清理本次失败上传的数据库记录
                m_dbWorker.enqueue([this, fileId = p->m_fileid, userId = p->m_userid]() {
                    m_sql->begin();
                    m_sql->execute("DELETE FROM user_file WHERE u_id=? AND f_id=?", {userId, fileId});
                    m_sql->execute("DELETE FROM files WHERE f_id=?", {fileId});
                    m_sql->commit();
                });
            }

            // 在锁保护下从列表中移除
            {
                std::lock_guard<std::mutex> lock(m_fileInfoMutex);
                auto it = std::find(m_lstFileInfo.begin(), m_lstFileInfo.end(), p);
                if (it != m_lstFileInfo.end()) m_lstFileInfo.erase(it);
            }
            delete p;
        }
    }
}

// ============================================================================
// 5.5 稀疏指纹预检 SPARSE FINGERPRINT PRE-CHECK — 三层上传漏斗中的 L2
// ============================================================================
// 客户端发送稀疏指纹（文件头 4KB + 文件尾 4KB + 文件大小，经 SHA-256 处理）。
// 服务端在内存集合中查找。若判定"肯定是新文件"，客户端可跳过
// 对整个文件计算完整 SHA-256（对大型文件收益显著）。
void tcpkernel::sparsecheckrq(SOCKET sock, const char* szbuf, int nlen)
{
    auto req = ProtocolFactory::deserializeSparseCheckRQ(szbuf + 1, nlen - 1);

    STRU_SPARSECHECKRS rs;
    // 在内存集合中查找：互斥锁保护下的 O(1) 查询
    std::string fp(req.m_szSparseFingerprint);
    {
        std::lock_guard<std::mutex> lock(m_sparseFpMutex);
        if (!fp.empty() && m_sparseFingerprints.count(fp)) {
            rs.m_szResult = 1;  // 可能存在 → 客户端应计算完整 SHA-256
        } else {
            rs.m_szResult = 0;  // 肯定是新文件 → 跳过完整 SHA-256
        }
    }
    auto pkt = ProtocolFactory::serializeSparseCheckRS(rs);
    m_server->sendData(sock, (const char*)pkt.data(), (int)pkt.size());
}

// ============================================================================
// 6. 下载文件信息 DOWNLOAD FILE INFO — Bug #6 修复：JOIN user_file，参数化查询
// ============================================================================
void tcpkernel::downloadfileinforq(SOCKET sock, const char *szbuf, int nlen)
{
    auto req = ProtocolFactory::deserializeDownloadFileInfoRQ(szbuf + 1, nlen - 1);

    // --- 重定向检查（仅当 fileID 已知时） ---
    if (m_nodeMgr && req.m_fileID > 0 && !m_nodeMgr->isLocal(req.m_fileID)) {
        STRU_REDIRECTRS redirect;
        strncpy(redirect.m_szRedirectIP, m_nodeMgr->getRedirectIP(req.m_fileID).c_str(), 15);
        redirect.m_szRedirectIP[15] = '\0';
        redirect.m_nRedirectPort = m_nodeMgr->getRedirectPort(req.m_fileID);
        redirect.m_szResult = _redirect_permanent;

        auto packet = ProtocolFactory::serializeRedirectRS(redirect);
        m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
        return;
    }

    std::string fname(req.m_szFileName);
    int64_t userId = req.m_userId;

    m_dbWorker.enqueue([this, sock, req, fname, userId]() {
        // F7-2 修复：零初始化，避免发送未初始化的栈内存。
        // 注意：`= {}` 不会清零成员，因为该结构体有用户自定义的
        // 默认构造函数（值初始化只会调用该构造函数，字段仍保持未设置状态），
        // 因此这里显式 memset，随后恢复 m_ntype。
        STRU_DOWNLOADFILEINFORS rs;
        memset(&rs, 0, sizeof(rs));
        rs.m_ntype = static_cast<char>(_default_protocol_downloadfileinfo_rs);
        rs.m_nBlockSize = MAXFILECONTENT;

        // Bug #6 修复：JOIN user_file——files 表没有 u_id 列
        std::list<std::string> lst;
        m_sql->query(
            "SELECT f.f_id, f.f_size, f.f_sha256 FROM files f "
            "JOIN user_file uf ON f.f_id = uf.f_id "
            "WHERE uf.u_id = ? AND f.f_name = ?",
            {userId, fname}, 3, lst);

        if (lst.size() >= 3) {
            rs.m_fileID = atoll(lst.front().c_str()); lst.pop_front();
            rs.m_fileSize = atoll(lst.front().c_str()); lst.pop_front();
            // F7-2：填充 SHA-256，便于客户端下载后校验完整性
            std::string sha256 = lst.front(); lst.pop_front();
            strncpy(rs.m_szFileSHA256, sha256.c_str(), sizeof(rs.m_szFileSHA256) - 1);
            rs.m_nBlockNum = (rs.m_fileSize > 0) ?
                (int)((rs.m_fileSize + rs.m_nBlockSize - 1) / rs.m_nBlockSize) : 1;
            rs.m_szResult = 1;
        }

        auto packet = ProtocolFactory::serializeDownloadFileInfoRS(rs);
        m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
    });
}

// ============================================================================
// 7. 下载文件块 DOWNLOAD FILE BLOCK — 参数化查询文件路径
// ============================================================================
void tcpkernel::downloadfileblockrq(SOCKET sock, const char *szbuf, int nlen)
{
    auto req = ProtocolFactory::deserializeDownloadFileBlockRQ(szbuf + 1, nlen - 1);

    m_dbWorker.enqueue([this, sock, req]() {
        STRU_DOWNLOADFILEBLOCKRS rs;
        rs.m_fileID = req.m_fileID;
        rs.m_pos = req.m_pos;
        rs.m_fileblocksize = 0;
        rs.m_szResult = 0;

        // 鉴权：验证该用户拥有此文件
        std::list<std::string> authLst;
        m_sql->query("SELECT 1 FROM user_file WHERE u_id=? AND f_id=?",
            {req.m_userId, req.m_fileID}, 1, authLst);
        if (authLst.empty()) {
            auto packet = ProtocolFactory::serializeDownloadFileBlockRS(rs);
            m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
            return;
        }

        // 从 FileStorage（blocks.dat）读取，而非遗留的文件路径
        int blockSeq = (int)(req.m_pos / MAXFILECONTENT);
        std::string blockData = m_storage->readBlock(req.m_fileID, blockSeq);
        if (!blockData.empty()) {
            size_t toCopy = (blockData.size() < MAXFILECONTENT) ? blockData.size() : (size_t)MAXFILECONTENT;
            memcpy(rs.m_szFileContent, blockData.data(), toCopy);
            rs.m_fileblocksize = (int64_t)toCopy;
            rs.m_szResult = 1;
        }

        auto packet = ProtocolFactory::serializeDownloadFileBlockRS(rs);
        m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
    });
}

// ============================================================================
// 8. 删除文件 DELETE FILE — Bug #6 修复：使用 user_file JOIN 做所有权检查
// ============================================================================
void tcpkernel::deletefilerq(SOCKET sock, const char *szbuf, int nlen)
{
    auto req = ProtocolFactory::deserializeDeleteFileRQ(szbuf + 1, nlen - 1);
    int64_t userId = req.m_userId;
    int64_t fileId = req.m_fileID;

    // --- 重定向检查 ---
    if (m_nodeMgr && !m_nodeMgr->isLocal(fileId)) {
        STRU_REDIRECTRS redirect;
        strncpy(redirect.m_szRedirectIP, m_nodeMgr->getRedirectIP(fileId).c_str(), 15);
        redirect.m_szRedirectIP[15] = '\0';
        redirect.m_nRedirectPort = m_nodeMgr->getRedirectPort(fileId);
        redirect.m_szResult = _redirect_permanent;

        auto packet = ProtocolFactory::serializeRedirectRS(redirect);
        m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
        return;
    }

    m_dbWorker.enqueue([this, sock, fileId, userId]() {
        STRU_DELETEFILERS rs;
        rs.m_fileID = fileId;
        rs.m_szResult = 0;

        // Bug #6 修复：通过 user_file JOIN 检查所有权（而非 files.u_id）
        std::list<std::string> lst;
        m_sql->query(
            "SELECT uf.f_id FROM user_file uf "
            "JOIN files f ON f.f_id = uf.f_id "
            "WHERE uf.u_id = ? AND uf.f_id = ?",
            {userId, fileId}, 1, lst);

        if (!lst.empty()) {
            // F8-2 修复：将删除操作包裹在事务中以保证原子性
            m_sql->begin();

            // F8-3 修复：检查返回值——仅当两条查询都成功时才报告成功
            bool ok1 = m_sql->execute(
                "UPDATE files SET fcount = fcount - 1 WHERE f_id = ? AND fcount > 0",
                {fileId});

            bool ok2 = m_sql->execute(
                "DELETE FROM user_file WHERE u_id = ? AND f_id = ?",
                {userId, fileId});

            if (ok1 && ok2) {
                rs.m_szResult = 1;
                m_sql->commit();
            } else {
                m_sql->rollback();
            }

            // --- 孤儿数据清理：当 fcount 归零时，清理所有相关数据 ---
            if (ok1 && ok2) {
                std::list<std::string> fcntLst;
                m_sql->query("SELECT fcount FROM files WHERE f_id = ?", {fileId}, 1, fcntLst);
                if (!fcntLst.empty() && atoll(fcntLst.front().c_str()) == 0) {
                    m_sql->begin();
                    // 删除依赖的数据库行（若定义了外键，CASCADE 本可完成此操作）
                    bool cascadeOk = true;
                    cascadeOk = m_sql->execute("DELETE FROM file_embeddings WHERE f_id = ?", {fileId}) && cascadeOk;
                    cascadeOk = m_sql->execute("DELETE FROM file_tags WHERE f_id = ?", {fileId}) && cascadeOk;
                    cascadeOk = m_sql->execute("DELETE FROM ai_previews WHERE f_id = ?", {fileId}) && cascadeOk;
                    cascadeOk = m_sql->execute("DELETE FROM share_links WHERE f_id = ?", {fileId}) && cascadeOk;
                    cascadeOk = m_sql->execute("DELETE FROM files WHERE f_id = ?", {fileId}) && cascadeOk;
                    if (cascadeOk) {
                        m_sql->commit();
                        // 将 FileStorage 中的数据块标记为已删除（以 # 前缀标记索引条目）
                        if (m_storage) {
                            m_storage->deleteFile(fileId);
                        }
                    } else {
                        m_sql->rollback();
                    }
                }
            }

            auto packet = ProtocolFactory::serializeDeleteFileRS(rs);
            m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
        }
    });
}

// ============================================================================
// 9. 分享文件 SHARE FILE — 参数化插入 + 使用 executeRaw 执行 DDL
// ============================================================================
void tcpkernel::sharefilerq(SOCKET sock, const char *szbuf, int nlen)
{
    auto req = ProtocolFactory::deserializeShareFileRQ(szbuf + 1, nlen - 1);
    int64_t userId = req.m_userId;
    int64_t fileId = req.m_fileID;

    // --- 重定向检查 ---
    if (m_nodeMgr && !m_nodeMgr->isLocal(fileId)) {
        STRU_REDIRECTRS redirect;
        strncpy(redirect.m_szRedirectIP, m_nodeMgr->getRedirectIP(fileId).c_str(), 15);
        redirect.m_szRedirectIP[15] = '\0';
        redirect.m_nRedirectPort = m_nodeMgr->getRedirectPort(fileId);
        redirect.m_szResult = _redirect_permanent;

        auto packet = ProtocolFactory::serializeRedirectRS(redirect);
        m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
        return;
    }

    m_dbWorker.enqueue([this, sock, fileId, userId]() {
        STRU_SHAREFILERS rs;
        rs.m_fileID = fileId;
        rs.m_szResult = 0;
        memset(rs.m_szShareCode, 0, sizeof(rs.m_szShareCode));

        // --- 鉴权：验证用户拥有该文件 ---
        std::list<std::string> ownLst;
        m_sql->query(
            "SELECT 1 FROM user_file WHERE u_id=? AND f_id=?",
            {userId, fileId}, 1, ownLst);
        if (ownLst.empty()) {
            // 用户不拥有此文件——拒绝
            auto packet = ProtocolFactory::serializeShareFileRS(rs);
            m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
            return;
        }

        // F10-5：重复分享检测——已分享则返回已有分享码
        {
            std::list<std::string> existLst;
            m_sql->query(
                "SELECT share_code FROM share_links WHERE f_id=? AND u_id=?",
                {fileId, userId}, 1, existLst);
            if (!existLst.empty()) {
                rs.m_szResult = 1;
                strcpy(rs.m_szShareCode, existLst.front().c_str());
                auto packet = ProtocolFactory::serializeShareFileRS(rs);
                m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
                return;
            }
        }

        // 若 share_links 表不存在则创建（DDL——使用原始执行接口）
        m_sql->executeRaw(
            "CREATE TABLE IF NOT EXISTS share_links("
            "share_code CHAR(8) PRIMARY KEY,"
            "f_id BIGINT NOT NULL,"
            "u_id BIGINT NOT NULL,"
            "created_at DATETIME DEFAULT NOW(),"
            "expires_at DATETIME NULL)");

        // 生成唯一分享码，冲突时重试。
        // std::random_device 在 MinGW 上具有确定性（生成的分享码恒定，
        // 导致第二个分享在 share_code 主键上冲突），因此改用
        // 墙钟时间 + fileId + 进程内单调计数器 + 重试轮次的确定性混合。
        static unsigned int s_shareSeq = 0;
        std::string shareCode;
        bool inserted = false;
        for (int retry = 0; retry < 5 && !inserted; retry++) {
            unsigned int rnd =
                (unsigned int)(time(nullptr) * 2654435761u)
                ^ (unsigned int)(fileId * 0x9E3779B9u)
                ^ (++s_shareSeq)
                ^ (unsigned int)(retry * 0xDEADBEEFu);
            char code[9];
            snprintf(code, sizeof(code), "%08x", rnd);
            shareCode = code;
            strcpy(rs.m_szShareCode, shareCode.c_str());

            inserted = m_sql->execute(
                "INSERT INTO share_links(share_code,f_id,u_id) VALUES(?,?,?)",
                {shareCode, fileId, userId});
        }

        rs.m_szResult = inserted ? 1 : 0;

        auto packet = ProtocolFactory::serializeShareFileRS(rs);
        m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
    });
}

// ============================================================================
// 10. 删除分享 DELETE SHARE / 撤销 — F10-4 修复
// ============================================================================
void tcpkernel::deletesharerq(SOCKET sock, const char *szbuf, int nlen)
{
    auto req = ProtocolFactory::deserializeDeleteShareRQ(szbuf + 1, nlen - 1);
    int64_t userId = req.m_userId;
    int64_t fileId = req.m_fileID;

    m_dbWorker.enqueue([this, sock, userId, fileId]() {
        STRU_DELETESHARERS rs;
        rs.m_fileID = fileId;
        rs.m_szResult = 0;

        // 若该用户拥有此分享链接则删除
        bool ok = m_sql->execute(
            "DELETE FROM share_links WHERE f_id=? AND u_id=?",
            {fileId, userId});

        rs.m_szResult = ok ? 1 : 0;

        auto packet = ProtocolFactory::serializeDeleteShareRS(rs);
        m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
    });
}

// ============================================================================
// 10. 获取文件 GET FILE / 提取 EXTRACT — 参数化查询（已重新编号为 11）
// ============================================================================
void tcpkernel::getfilerq(SOCKET sock, const char *szbuf, int nlen)
{
    auto req = ProtocolFactory::deserializeGetFileRQ(szbuf + 1, nlen - 1);
    int64_t shareFileID = req.m_shareFileID;
    int64_t userId = req.m_userId;

    // 将 m_shareFileID（承载十六进制分享码的 int64_t）还原为十六进制字符串
    char shareCode[9];
    snprintf(shareCode, sizeof(shareCode), "%08llx", (unsigned long long)shareFileID);
    std::string sc(shareCode);

    m_dbWorker.enqueue([this, sock, sc, userId, shareFileID]() {
        STRU_GETFILERS rs;
        rs.m_pos = 0;
        rs.m_szResult = 0;
        rs.m_fileID = 0;

        // 查找分享码（并执行过期检查）
        std::list<std::string> lst;
        m_sql->query(
            "SELECT sl.f_id, sl.u_id FROM share_links sl "
            "WHERE sl.share_code=? AND (sl.expires_at IS NULL OR sl.expires_at > NOW())",
            {sc}, 2, lst);

        if (!lst.empty()) {
            int64_t sharedFileId = atoll(lst.front().c_str()); lst.pop_front();
            int64_t sharerUserId = atoll(lst.front().c_str());

            // --- 防护 1：禁止提取自己分享的文件 ---
            if (sharerUserId == userId) {
                // 返回错误——不能提取自己分享的文件
                rs.m_szResult = 0;
                auto packet = ProtocolFactory::serializeGetFileRS(rs);
                m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
                return;
            }

            // --- 防护 2：防止重复提取 ---
            std::list<std::string> dupLst;
            m_sql->query("SELECT 1 FROM user_file WHERE u_id=? AND f_id=?",
                {userId, sharedFileId}, 1, dupLst);
            if (!dupLst.empty()) {
                // 已提取过——返回成功及已有文件信息
                rs.m_fileID = sharedFileId;
                rs.m_szResult = 1;
                std::list<std::string> flst;
                m_sql->query("SELECT f_name,f_size,f_sha256 FROM files WHERE f_id=?",
                    {sharedFileId}, 3, flst);
                if (flst.size() >= 3) {
                    strcpy(rs.m_fileInfo.m_szFileName, flst.front().c_str()); flst.pop_front();
                    rs.m_fileInfo.m_filesize = atoll(flst.front().c_str()); flst.pop_front();
                    strcpy(rs.m_szFileSHA256, flst.front().c_str());
                }
                auto packet = ProtocolFactory::serializeGetFileRS(rs);
                m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
                return;
            }

            // 获取文件信息
            std::list<std::string> flst;
            m_sql->query("SELECT f_name,f_size,f_sha256 FROM files WHERE f_id=?",
                {sharedFileId}, 3, flst);

            if (flst.size() >= 3) {
                strcpy(rs.m_fileInfo.m_szFileName, flst.front().c_str()); flst.pop_front();
                rs.m_fileInfo.m_filesize = atoll(flst.front().c_str()); flst.pop_front();
                strcpy(rs.m_szFileSHA256, flst.front().c_str());

                // --- 引用语义（非复制）：复用同一条文件记录 ---
                // 创建指向被分享文件的用户-文件映射
                m_sql->execute(
                    "INSERT INTO user_file(u_id,f_id) VALUES(?,?)",
                    {userId, sharedFileId});

                // 文件的引用计数加一
                m_sql->execute(
                    "UPDATE files SET fcount = fcount + 1 WHERE f_id = ?",
                    {sharedFileId});

                rs.m_fileID = sharedFileId;
                rs.m_szResult = 1;
            }
        }

        auto packet = ProtocolFactory::serializeGetFileRS(rs);
        m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
    });
}

// ============================================================================
// Bloom Filter 预热 — 参数化查询
// ============================================================================
void tcpkernel::warmupBloomFilter()
{
    // 在 DbWorker 启动前同步执行，避免连接冲突
    std::list<std::string> sha256s;
    m_sql->query("SELECT f_sha256 FROM files", {}, 1, sha256s);

    for (const auto& sha : sha256s) {
        m_bloomFilter->insert(sha);
    }

    printf("BloomFilter warmup: %zu hashes loaded, %zu bits, %zu hash functions\n",
           sha256s.size(), m_bloomFilter->size(), m_bloomFilter->hashCount());
}

// ============================================================================
// 集群：处理来自对等节点的复制数据块
// ============================================================================
void tcpkernel::replicateblockrq(SOCKET sock, const char* szbuf, int nlen)
{
    auto req = ProtocolFactory::deserializeReplicateBlockRQ(szbuf + 1, nlen - 1);

    STRU_REPLICATEBLOCKRS rs;
    rs.m_fileId = req.m_fileId;
    rs.m_blockSeq = req.m_blockSeq;
    rs.m_szResult = 0;

    if (m_storage) {
        int64_t offset = m_storage->writeBlock(req.m_fileId, req.m_blockSeq,
            req.m_szData, (int)req.m_dataLen);

        if (offset >= 0) {
            rs.m_szResult = 1;
            printf("Replicate: stored block seq=%d for file %lld at offset %lld\n",
                req.m_blockSeq, (long long)req.m_fileId, (long long)offset);
        }
    }

    auto packet = ProtocolFactory::serializeReplicateBlockRS(rs);
    m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
}

// ============================================================================
// AI：智能预览处理函数（阶段三 Sprint 3.2）
// ============================================================================
// ============================================================================
// 阶段二：HTTP 流媒体令牌处理函数
// ============================================================================
void tcpkernel::streamtokenrq(SOCKET sock, const char* szbuf, int nlen) {
    auto req = ProtocolFactory::deserializeStreamTokenRQ(szbuf + 1, nlen - 1);

    // 确定 HTTP 端口：若文件位于对等节点，则使用该节点的 HTTP 端口，
    // 使流媒体 URL 直接指向持有数据的节点
    int httpPort = m_httpPort;
    if (m_nodeMgr && req.m_fileID > 0 && !m_nodeMgr->isLocal(req.m_fileID)) {
        int peerPort = m_nodeMgr->getHttpPortForFile(req.m_fileID);
        if (peerPort > 0) {
            httpPort = peerPort;
            printf("[StreamToken] Redirecting stream for file=%lld to peer HTTP port %d\n",
                   (long long)req.m_fileID, peerPort);
        }
    }

    // 在数据库 worker 线程上查询文件信息以获取文件名
    m_dbWorker.enqueue([this, sock, req, httpPort]() {
        STRU_STREAMTOKENRS rs = {};  // 值初始化所有字段为 0
        rs.m_fileID = req.m_fileID;
        rs.m_nHttpPort = httpPort;
        rs.m_szResult = 1;  // 默认值：错误

        // 从 MySQL 查询文件名——通过 user_file JOIN 验证所有权
        std::list<std::string> lst;
        m_sql->query(
            "SELECT f.f_name, f.f_size FROM files f "
            "JOIN user_file uf ON f.f_id = uf.f_id "
            "WHERE uf.u_id = ? AND f.f_id = ?",
            {req.m_userId, req.m_fileID}, 2, lst);

        if (lst.size() >= 2) {
            auto it = lst.begin();
            strncpy(rs.m_szFileName, it->c_str(), sizeof(rs.m_szFileName) - 1);
            rs.m_szFileName[sizeof(rs.m_szFileName) - 1] = '\0';
            ++it;
            rs.m_fileSize = 0;
            try { rs.m_fileSize = std::stoll(*it); } catch (...) {}

            // 生成带时间戳的流媒体令牌
            int64_t ts = 0;
            std::string token = m_httpServer->generateToken(req.m_userId, req.m_fileID, ts);
            strncpy(rs.m_szToken, token.c_str(), sizeof(rs.m_szToken) - 1);
            rs.m_szToken[sizeof(rs.m_szToken) - 1] = '\0';
            rs.m_nTimestamp = ts;

            rs.m_szResult = 0;  // 成功

            printf("[StreamToken] Generated token for user=%lld file=%lld name=%s\n",
                   (long long)req.m_userId, (long long)req.m_fileID, rs.m_szFileName);
        } else {
            printf("[StreamToken] File not found: fileId=%lld\n", (long long)req.m_fileID);
        }

        // 序列化并通过 IOCP 主线程发送
        auto packet = ProtocolFactory::serializeStreamTokenRS(rs);
        auto* pkt = new std::vector<uint8_t>(std::move(packet));
        m_server->sendData(sock, (const char*)pkt->data(), (int)pkt->size());
        delete pkt;
    });
}

void tcpkernel::aipreviewrq(SOCKET sock, const char* szbuf, int nlen) {
    auto req = ProtocolFactory::deserializeAIPreviewRQ(szbuf + 1, nlen - 1);

    m_dbWorker.enqueue([this, sock, req]() {
        STRU_AIPREVIEWRS rs;
        rs.m_fileID = req.m_fileID;
        rs.m_szResult = 0;
        memset(rs.m_szSummary, 0, sizeof(rs.m_szSummary));
        memset(rs.m_szKeywords, 0, sizeof(rs.m_szKeywords));
        memset(rs.m_szKeySentences, 0, sizeof(rs.m_szKeySentences));
        memset(rs.m_szFileType, 0, sizeof(rs.m_szFileType));
        memset(rs.m_szFileName, 0, sizeof(rs.m_szFileName));
        rs.m_nRawContentLen = 0;
        memset(rs.m_szRawContent, 0, sizeof(rs.m_szRawContent));
        memset(rs.m_szAIError, 0, sizeof(rs.m_szAIError));

        // 从数据库获取文件名
        std::list<std::string> lst;
        m_sql->query(
            "SELECT f_name FROM files f JOIN user_file uf ON f.f_id=uf.f_id WHERE f.f_id=? AND uf.u_id=?",
            {static_cast<int64_t>(req.m_fileID), static_cast<int64_t>(req.m_userId)}, 1, lst);

        if (lst.empty()) {
            rs.m_szResult = 1;
            strcpy(rs.m_szSummary, "File not found");
        } else {
            std::string fileName = lst.front();

            // --- L1：检查 AI 预览缓存 ---
            bool cacheHit = false;
            {
                std::list<std::string> cacheRows;
                if (m_sql->query(
                    "SELECT summary, keywords, key_sentences, file_type FROM ai_previews WHERE f_id=?",
                    {static_cast<int64_t>(req.m_fileID)}, 4, cacheRows) && cacheRows.size() == 4) {
                    auto it = cacheRows.begin();
                    std::string cachedSummary = *it; ++it;
                    std::string cachedKeywords = *it; ++it;
                    std::string cachedKeySentences = *it; ++it;
                    std::string cachedFileType = *it;
                    cacheHit = true;

                    strncpy(rs.m_szSummary, cachedSummary.c_str(), sizeof(rs.m_szSummary) - 1);
                    strncpy(rs.m_szKeywords, cachedKeywords.c_str(), sizeof(rs.m_szKeywords) - 1);
                    strncpy(rs.m_szKeySentences, cachedKeySentences.c_str(), sizeof(rs.m_szKeySentences) - 1);
                    strncpy(rs.m_szFileType, cachedFileType.c_str(), sizeof(rs.m_szFileType) - 1);
                    rs.m_szResult = 0;
                }
            }

            // --- 读取文件内容（用于 rawContent 展示；缓存未命中时供 AI 使用） ---
            std::string content;
            if (m_storage) {
                content = m_storage->readBlock(req.m_fileID, 0);
            }
            // 遗留回退：尝试从磁盘路径读取
            if (content.empty()) {
                std::string fpath = std::string(m_szSystemPath) + std::to_string(req.m_userId) + "\\" + fileName;
                // F13-3 修复：读取前先检查文件大小，避免大文件导致内存溢出
                std::ifstream ff(fpath, std::ios::binary | std::ios::ate);
                if (ff) {
                    std::streamsize fsize = ff.tellg();
                    const std::streamsize MAX_LEGACY_READ = 10 * 1024 * 1024; // 10 MB 上限
                    if (fsize > MAX_LEGACY_READ) {
                        fprintf(stderr, "[aipreview] WARNING: legacy file too large (%lld bytes), "
                                "truncating to first %lld bytes\n",
                                (long long)fsize, (long long)MAX_LEGACY_READ);
                    }
                    ff.seekg(0, std::ios::beg);
                    std::vector<char> buf(static_cast<size_t>(std::min(fsize, MAX_LEGACY_READ)));
                    ff.read(buf.data(), buf.size());
                    content.assign(buf.data(), static_cast<size_t>(ff.gcount()));
                }
            }

            if (cacheHit) {
                // 缓存命中——跳过 AI，仅填充文件名与原始内容
                strncpy(rs.m_szFileName, fileName.c_str(), sizeof(rs.m_szFileName) - 1);
                int rawLen = std::min(static_cast<int>(content.size()), MAXFILECONTENT * 2);
                rs.m_nRawContentLen = rawLen;
                if (rawLen > 0) memcpy(rs.m_szRawContent, content.c_str(), rawLen);
                rs.m_szAIError[0] = '\0';  // 无错误，由缓存提供
            } else if (!content.empty()) {
                // ---- 主路径：本地统计式预览（关键词 / 关键句 / 首段摘要；无 Key 也完整可用）----
                const LocalPreviewResult localR =
                    LocalPreviewEngine::preview(TxtTextExtractor().extract(content), fileName);

                rs.m_szResult = 0;                       // 本地预览成功（不再依赖 AI 可用性）
                strncpy(rs.m_szSummary, localR.summary.c_str(), sizeof(rs.m_szSummary) - 1);
                strncpy(rs.m_szKeywords, localR.keywords.c_str(), sizeof(rs.m_szKeywords) - 1);
                strncpy(rs.m_szKeySentences, localR.keySentences.c_str(), sizeof(rs.m_szKeySentences) - 1);
                strncpy(rs.m_szFileType, localR.fileType.c_str(), sizeof(rs.m_szFileType) - 1);
                strncpy(rs.m_szFileName, fileName.c_str(), sizeof(rs.m_szFileName) - 1);
                int rawLen = std::min(static_cast<int>(content.size()), MAXFILECONTENT * 2);
                rs.m_nRawContentLen = rawLen;
                if (rawLen > 0) memcpy(rs.m_szRawContent, content.c_str(), rawLen);
                rs.m_szAIError[0] = '\0';   // AI 关闭或未失败时为空串（不再出现 "AI disabled" 文案）

                // 顺带把已读到的内容补进本地倒排索引：存量文件在被预览后即可内容级检索（幂等）
                indexLocalContent(req.m_fileID, fileName, content);

                // ---- AI 增强（可选）：真正成功才覆盖本地结果；失败/降级保留本地结果 ----
                // 注意：AIFilePreview::preview 在"AI 调用失败"时也会返回 success=true +
                // 自己那套降级值（summary=前 200 字符、keywords=文件名、fileType="unknown"、
                // errorMsg=失败原因）。本地引擎结果明显更优，因此这里把 errorMsg 非空视为
                // "AI 未真正生效"，只记入 m_szAIError，绝不覆盖、也不写缓存（写缓存会永久
                // 固化降级值，且缓存命中后不再重试 AI）。
                bool aiApplied = false;
                if (APIBridge::instance()->isEnabled() && m_aiPreview) {
                    auto result = m_aiPreview->preview(req.m_fileID, content, fileName);
                    const bool aiReallyOk = result.success && result.errorMsg.empty();
                    if (aiReallyOk) {
                        if (!result.summary.empty())
                            strncpy(rs.m_szSummary, result.summary.c_str(), sizeof(rs.m_szSummary) - 1);
                        if (!result.keywords.empty())
                            strncpy(rs.m_szKeywords, result.keywords.c_str(), sizeof(rs.m_szKeywords) - 1);
                        if (!result.keySentences.empty())
                            strncpy(rs.m_szKeySentences, result.keySentences.c_str(), sizeof(rs.m_szKeySentences) - 1);
                        aiApplied = true;
                    } else if (!result.errorMsg.empty()) {
                        // AI 失败：本地结果照常返回，仅把原因写入 m_szAIError 供客户端提示
                        strncpy(rs.m_szAIError, result.errorMsg.c_str(), sizeof(rs.m_szAIError) - 1);
                    }
                }

                // ---- 缓存写入（沿用现有 SQL 结构）：无 AI 时写本地结果，AI 真正成功时写增强结果 ----
                // AI 开启但调用失败 → 不写缓存：本地计算代价极低，保留"下次重试 AI"的机会。
                if (!APIBridge::instance()->isEnabled() || aiApplied) {
                    m_sql->execute(
                        "INSERT INTO ai_previews(f_id, summary, keywords, key_sentences, file_type) "
                        "VALUES(?,?,?,?,?) ON DUPLICATE KEY UPDATE "
                        "summary=VALUES(summary), keywords=VALUES(keywords), key_sentences=VALUES(key_sentences), file_type=VALUES(file_type)",
                        {static_cast<int64_t>(req.m_fileID),
                         std::string(rs.m_szSummary), std::string(rs.m_szKeywords),
                         std::string(rs.m_szKeySentences), std::string(rs.m_szFileType)});
                }
            } else {
                rs.m_szResult = 1;
                strcpy(rs.m_szSummary, "Cannot read file content");
            }
        }

        auto packet = ProtocolFactory::serializeAIPreviewRS(rs);
        m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
    });
}

// ============================================================================
// AI：语义搜索处理函数（阶段三 Sprint 3.3）
// ============================================================================
void tcpkernel::aisearchrq(SOCKET sock, const char* szbuf, int nlen) {
    auto req = ProtocolFactory::deserializeAISearchRQ(szbuf + 1, nlen - 1);

    m_dbWorker.enqueue([this, sock, req]() {
        STRU_AISEARCHRS rs;
        rs.m_nResultNum = 0;
        rs.m_szResult = 1; // 默认值：无任何命中（命中即置 0）

        std::string query(req.m_szQuery);

        // F9-1 修复：嵌入模式前转义 LIKE 特殊字符（%、_、\）
        auto escapeLike = [](const std::string& s) -> std::string {
            std::string out;
            for (char c : s) {
                if (c == '%' || c == '_' || c == '\\') out += '\\';
                out += c;
            }
            return out;
        };

        // 结果归并：三层结果按优先级追加，按 fileId 去重，最多 MAXSIZE 条。
        //   ① AI embedding 精排（可选增强，enabled 时优先）
        //   ② 本地内容级 TF-IDF 检索（**主路径**：倒排召回 + 余弦排序，无 Key 也完整可用）
        //   ③ 文件名 LIKE（兜底：存量未索引文件、二进制文件、中文子串直接命中文件名）
        std::set<int64_t> seenFileIds;
        auto appendResult = [&rs, &seenFileIds](int64_t fid, const std::string& name,
                                                int64_t size, const std::string& reason,
                                                const std::string& sha256) {
            if (fid <= 0 || rs.m_nResultNum >= MAXSIZE) return;
            if (!seenFileIds.insert(fid).second) return;   // 已在更高优先级层出现 → 跳过
            AI_SEARCH_RESULT& slot = rs.m_aryResults[rs.m_nResultNum];
            memset(&slot, 0, sizeof(slot));
            slot.m_fileInfo.m_fileID = fid;
            strncpy(slot.m_fileInfo.m_szFileName, name.c_str(), MAXSIZE - 1);
            slot.m_fileInfo.m_filesize = size;
            strncpy(slot.m_szMatchReason, reason.c_str(), sizeof(slot.m_szMatchReason) - 1);
            if (!sha256.empty())
                strncpy(slot.m_szFileSHA256, sha256.c_str(), sizeof(slot.m_szFileSHA256) - 1);
            rs.m_nResultNum++;
        };

        // ---- 层 ①：AI embedding 精排（AI 不可用/失败/无结果都不影响下面的本地主路径）----
        if (APIBridge::instance()->isEnabled() && m_aiSearch) {
            auto aiRs = m_aiSearch->search(query, req.m_userId);
            if (aiRs.m_szResult == 0 && aiRs.m_nResultNum > 0) {
                for (int i = 0; i < aiRs.m_nResultNum; ++i) {
                    const AI_SEARCH_RESULT& a = aiRs.m_aryResults[i];
                    appendResult(a.m_fileInfo.m_fileID, a.m_fileInfo.m_szFileName,
                                 a.m_fileInfo.m_filesize, a.m_szMatchReason,
                                 std::string(a.m_szFileSHA256));
                }
            }
        }

        // ---- 层 ②：本地内容级 TF-IDF 检索（主路径）----
        // 本地索引是"全库内容索引"，因此召回后必须用 user_file 过滤到该用户可见的文件，
        // 并同时取出文件名/大小/SHA-256 供回包（一次 SQL 完成过滤 + 取元数据）。
        if (m_localSearch) {
            const std::vector<LocalSearchEngine::Hit> hits =
                m_localSearch->search(query, kLocalSearchTopN);
            if (!hits.empty()) {
                std::map<int64_t, SearchFileMeta> meta;
                std::string sql = "SELECT f.f_id, f.f_name, f.f_size, f.f_sha256 FROM files f "
                                  "JOIN user_file uf ON f.f_id=uf.f_id "
                                  "WHERE uf.u_id=? AND f.f_id IN (";
                std::vector<SqlValue> params;
                params.push_back(static_cast<int64_t>(req.m_userId));
                for (size_t i = 0; i < hits.size(); ++i) {
                    if (i) sql += ",";
                    sql += "?";
                    params.push_back(hits[i].fileId);   // 参数化：fileId 不进 SQL 文本
                }
                sql += ")";

                std::list<std::string> rows;
                m_sql->query(sql.c_str(), params, 4, rows);
                while (rows.size() >= 4) {
                    SearchFileMeta m;
                    m.fileId = atoll(rows.front().c_str()); rows.pop_front();
                    m.name   = rows.front();                rows.pop_front();
                    m.size   = atoll(rows.front().c_str()); rows.pop_front();
                    m.sha256 = rows.front();                rows.pop_front();
                    meta[m.fileId] = m;
                }

                // 按余弦分数降序输出（hits 本身已排序）
                for (size_t i = 0; i < hits.size(); ++i) {
                    std::map<int64_t, SearchFileMeta>::const_iterator it = meta.find(hits[i].fileId);
                    if (it == meta.end()) continue;     // 不属于该用户 / 文件已删除
                    char reason[64];
                    snprintf(reason, sizeof(reason), "内容匹配度 %.0f%%", hits[i].score * 100.0);
                    appendResult(it->second.fileId, it->second.name, it->second.size,
                                 reason, it->second.sha256);
                }
            }
        }

        // ---- 层 ③：文件名 LIKE 兜底（保持 F9-2 合并结构，始终执行）----
        {
            std::list<std::string> lst;
            std::string escapedQuery = escapeLike(query);
            std::string likeQuery = "%" + escapedQuery + "%";
            m_sql->query(
                "SELECT f.f_id, f.f_name, f.f_size FROM files f "
                "JOIN user_file uf ON f.f_id=uf.f_id WHERE uf.u_id=? AND f.f_name LIKE ? LIMIT 45",
                {static_cast<int64_t>(req.m_userId), likeQuery}, 3, lst);

            while (lst.size() >= 3) {
                int64_t fid = atoll(lst.front().c_str()); lst.pop_front();
                std::string fname = lst.front(); lst.pop_front();
                int64_t fsize = atoll(lst.front().c_str()); lst.pop_front();
                appendResult(fid, fname, fsize, "filename match", std::string());
            }
        }

        // 有命中即为成功（本地主路径的结果也是"完整功能"，不再是降级）
        if (rs.m_nResultNum > 0) rs.m_szResult = 0;

        auto packet = ProtocolFactory::serializeAISearchRS(rs);
        m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
    });
}

// ============================================================================
// AI：自动标签处理函数（阶段三 Sprint 3.4）
// ============================================================================
void tcpkernel::aitagrq(SOCKET sock, const char* szbuf, int nlen) {
    auto req = ProtocolFactory::deserializeAITagRQ(szbuf + 1, nlen - 1);

    m_dbWorker.enqueue([this, sock, req]() {
        STRU_AITAGRS rs;
        rs.m_fileID = req.m_fileID;
        rs.m_szResult = 0;
        rs.m_nTagNum = 0;
        rs.m_nNewTagSuggestions = 0;
        memset(rs.m_szTags, 0, sizeof(rs.m_szTags));
        memset(rs.m_szNewTags, 0, sizeof(rs.m_szNewTags));

        // 获取文件信息
        std::list<std::string> lst;
        m_sql->query("SELECT f_name FROM files WHERE f_id=?",
            {static_cast<int64_t>(req.m_fileID)}, 1, lst);

        if (!lst.empty()) {
            std::string fileName = lst.front();

            // F15-2 修复：先检查缓存——不调用 AI 直接返回已有标签
            std::list<std::string> cachedTags;
            m_sql->query("SELECT tag FROM file_tags WHERE f_id=? LIMIT 15",
                {static_cast<int64_t>(req.m_fileID)}, 1, cachedTags);
            if (!cachedTags.empty()) {
                for (const auto& tag : cachedTags) {
                    if (rs.m_nTagNum < 15) {
                        strncpy(rs.m_szTags[rs.m_nTagNum], tag.c_str(), MAXSIZE - 1);
                        rs.m_nTagNum++;
                    }
                }
                auto packet = ProtocolFactory::serializeAITagRS(rs);
                m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
                return;
            }

            std::string content;
            if (m_storage) {
                content = m_storage->readBlock(req.m_fileID, 0);
            }

            // ---- 主路径：本地规则 + 词典标签（无 Key 也非空：扩展名规则 / 原始扩展名兜底）----
            std::vector<std::string> tagList = localTagsWithFallback(
                m_localTag, TxtTextExtractor().extract(content), fileName);

            if (!content.empty() && APIBridge::instance()->isEnabled() && m_aiTag) {
                // ---- AI 增强（可选）：LLM 标签与规则标签合并去重（规则在前，AI 追加）；
                //      AI 不可用/失败 → 纯规则结果照常返回 ----
                auto tagResult = m_aiTag->tagFile(req.m_fileID, content, fileName);
                if (tagResult.success) {
                    for (size_t i = 0; i < tagResult.tags.size(); ++i) {
                        // AITagService::tagFile 在"AI 未启用或 chat 失败"时会返回 success=true
                        // + 占位标签 "WeiFenLei"（未分类）：主路径已是规则引擎，不再并入占位标签
                        if (tagResult.tags[i] == "WeiFenLei")
                            continue;
                        pushUniqueTag(tagList, tagResult.tags[i]);
                    }
                    for (const auto& newTag : tagResult.newTagSuggestions) {
                        if (rs.m_nNewTagSuggestions < 5) {
                            strncpy(rs.m_szNewTags[rs.m_nNewTagSuggestions], newTag.c_str(), MAXSIZE - 1);
                            rs.m_nNewTagSuggestions++;
                        }
                    }
                }
            }

            // ---- 落库（沿用现有 SQL：file_tags / tag_pool）+ 填充响应 ----
            const bool persist = !content.empty();   // 无内容路径保持旧行为：只回包不落库
            if (persist) {
                // 清除该文件的旧标签
                m_sql->execute("DELETE FROM file_tags WHERE f_id=?",
                               {static_cast<int64_t>(req.m_fileID)});
            }
            for (size_t i = 0; i < tagList.size(); ++i) {
                // 限制标签长度
                std::string safeTag = tagList[i].size() > 99 ? tagList[i].substr(0, 99) : tagList[i];
                if (persist) {
                    m_sql->execute(
                        "INSERT IGNORE INTO file_tags(f_id, tag) VALUES(?,?)",
                        {static_cast<int64_t>(req.m_fileID), safeTag});
                    // 更新 tag_pool：use_count 加一，达到阈值后升级为 active
                    m_sql->execute(
                        "INSERT INTO tag_pool(tag, status, use_count, last_used_at) "
                        "VALUES(?,'pending',1,NOW()) ON DUPLICATE KEY UPDATE "
                        "use_count=use_count+1, last_used_at=NOW(), "
                        "status=IF(use_count >= 3, 'active', status)",
                        {safeTag});
                }
                if (rs.m_nTagNum < 15) {
                    strncpy(rs.m_szTags[rs.m_nTagNum], safeTag.c_str(), MAXSIZE - 1);
                    rs.m_nTagNum++;
                }
            }
        }

        auto packet = ProtocolFactory::serializeAITagRS(rs);
        m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
    });
}
