#include "tcpkernel.h"
#include "../config/ConfigParser.h"
#include <QCoreApplication>
#include <QStringList>
#include <algorithm>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <random>   // CSPRNG for share code generation
#include <set>      // F9-2: dedup AI + LIKE search results

Ikernel *tcpkernel::m_kernel=new tcpkernel;

// Thread-safe helpers for m_lstFileInfo (accessed from IOCP workers + DbWorker thread)
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
    m_httpPort = 0;  // set in boolopen() from server.conf
    m_szSystemPath[0] = '\0';  // set in boolopen() from server.conf
}

tcpkernel::~tcpkernel()
{
    m_dbWorker.stop();
    m_replWorker.stop();  // Must stop before delete m_nodeMgr — worker thread uses NodeManager

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
}

bool tcpkernel::boolopen()
{
    // ===================================================================
    // Read ALL settings from server.conf (config-driven, no hardcodes)
    // Config path: --config <path> argument, or "server.conf" by default
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

    // Apply defaults for any missing values
    if (listenIP.empty())    listenIP    = "127.0.0.1";
    if (listenPort == 0)     listenPort  = 8899;
    if (workerCount == 0)    workerCount = 4;
    if (storagePath.empty()) storagePath = "D:\\disk1\\";
    if (mysqlHost.empty())   mysqlHost   = "localhost";
    if (mysqlUser.empty())   mysqlUser   = "root";
    if (mysqlDb.empty())     mysqlDb     = "new_schema15";
    if (httpPort == 0)       httpPort    = 8900;

    // Store in members for use by handlers
    m_httpPort = httpPort;
    strcpy(m_szSystemPath, storagePath.c_str());

    printf("Config loaded: listen=%s:%d workers=%d storage=%s mysql=%s@%s/%s http=%d\n",
           listenIP.c_str(), listenPort, workerCount, storagePath.c_str(),
           mysqlUser.c_str(), mysqlHost.c_str(), mysqlDb.c_str(), httpPort);

    // --- AI config (optional) ---
    {
        std::string aiSection = jsonGetObject(configJson, "ai");
        if (!aiSection.empty()) {
            std::string aiProvider = jsonGetString(aiSection, "provider");
            std::string aiBaseUrl = jsonGetString(aiSection, "base_url");
            std::string aiChatModel = jsonGetString(aiSection, "chat_model");
            std::string aiEmbedModel = jsonGetString(aiSection, "embedding_model");
            // Optional api_key field in server.conf (env var still takes priority)
            std::string aiApiKey = jsonGetString(aiSection, "api_key");

            // Key priority unchanged: env var first, then optional server.conf api_key
            const char* envKey = nullptr;
            if (aiProvider == "siliconflow" || aiProvider.empty()) {
                envKey = std::getenv("SILICONFLOW_API_KEY");
                if (!envKey) envKey = std::getenv("OPENAI_API_KEY");  // fallback
            } else if (aiProvider == "openai") {
                envKey = std::getenv("OPENAI_API_KEY");
            }
            std::string apiKey = (envKey && strlen(envKey) > 0) ? std::string(envKey) : aiApiKey;

            // Ollama is a local service: no API key required; others need a key from env or config
            bool isOllama = (aiProvider == "ollama");
            if (isOllama || !apiKey.empty()) {
                AiConfig aiCfg;
                aiCfg.provider = aiProvider.empty() ? "siliconflow" : aiProvider;
                aiCfg.apiKey = apiKey;
                aiCfg.baseUrl = aiBaseUrl.empty() ? "https://api.siliconflow.cn/v1" : aiBaseUrl;
                aiCfg.chatModel = aiChatModel.empty() ? "deepseek-ai/DeepSeek-V3" : aiChatModel;
                aiCfg.embeddingModel = aiEmbedModel.empty() ? "BAAI/bge-m3" : aiEmbedModel;

                // Ollama defaults (native /api/chat + /api/embed protocol)
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
        // If no "ai" section in config, APIBridge already tried env vars in constructor
    }

    // --- IocpServer: set callbacks before start ---
    m_server->setDataCallback([this](SOCKET sock, const char* data, int len) {
        this->dealData(sock, data, len);
    });
    m_server->setDisconnectCallback([this](SOCKET sock) {
        (void)sock;  // TODO: track socket→user mapping for per-connection cleanup
        // CRITICAL FIX: Do NOT clear ALL uploads when ANY single client disconnects.
        // Each entry must be tied to a specific socket for scoped cleanup.
    });

    if (!m_server->start(listenIP.c_str(), listenPort, workerCount)) {
        printf("IocpServer start failed\n");
        return false;
    }

    // --- MySqlWrapper: connect ---
    if (!m_sql->connect(mysqlHost.c_str(), mysqlUser.c_str(), mysqlPass.c_str(), mysqlDb.c_str())) {
        printf("MySQL connect failed\n");
        return false;
    }

    // --- Bloom Filter warmup (synchronous, before DbWorker starts) ---
    m_bloomFilter = new BloomFilter();
    warmupBloomFilter();

    // --- L2 Sparse Fingerprint warmup (load known fingerprints into memory) ---
    {
        std::list<std::string> sparseList;
        m_sql->query("SELECT f_sparse_sha256 FROM files WHERE f_sparse_sha256 != ''",
                     {}, 1, sparseList);
        for (const auto& fp : sparseList) {
            if (!fp.empty()) m_sparseFingerprints.insert(fp);
        }
        printf("Sparse fingerprints warmed up: %zu loaded\n", m_sparseFingerprints.size());
    }

    // --- DbWorker: start async DB thread (after warmup to avoid conn conflict) ---
    m_dbWorker.start();

    // --- Cluster NodeManager ---
    if (!m_nodeMgr->init(configPath.c_str())) {
        printf("NodeManager: init failed, running in standalone mode\n");
        // Non-fatal — continue as standalone
    }

    // --- ReplicationWorker: async cross-node block replication ---
    m_replWorker.start(m_nodeMgr);

    // --- FileStorage ---
    m_storage = new FileStorage();
    if (!m_storage->init(storagePath.c_str())) {
        printf("FileStorage init failed\n");
        return false;
    }

    // --- SQLite upload state for resume upload ---
    m_uploadState = new SqliteState();
    {
        std::string uploadDbPath = storagePath + "upload_state.db";
        if (!m_uploadState->open(uploadDbPath.c_str())) {
            printf("SqliteState: failed to open database\n");
            // Non-fatal
        }
    }

    // Recover unfinished uploads
    recoverUploads();

    // --- HTTP Streaming Server (video/audio/picture online playback) ---
    m_httpServer->start(httpPort, m_storage);

    // --- Init APIBridge (reads OPENAI_API_KEY from environment) ---
    APIBridge::instance();  // singleton init — logs whether AI is enabled

    return true;
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
        FILE* f = fopen(s.tempPath.c_str(), "rb");
        if (!f) {
            m_uploadState->setState(s.fileHash, s.userId, "abandoned");
            // F6-4 fix: clean up orphan blocks from FileStorage when abandoning upload
            if (m_storage && s.fileId > 0) {
                m_storage->deleteFile(s.fileId);
                printf("  Abandoned+cleaned: temp file missing, deleted orphan blocks for fileId=%lld\n",
                       s.fileId);
            } else {
                printf("  Abandoned: temp file missing for hash %s\n", s.fileHash.c_str());
            }
            continue;
        }
        fclose(f);
        printf("  Resumable: user=%lld file=%lld offset=%lld/%lld\n",
               s.userId, s.fileId, s.lastOffset, s.fileSize);
    }
}

bool tcpkernel::dealData(SOCKET sock, const char *szbuf, int nlen)
{
    // CRITICAL: Wrap entire dispatch in try-catch to prevent malformed packets
    // from crashing the server. BinaryStream deserialization throws std::runtime_error
    // on truncated/corrupt data; without this catch, an unhandled exception in an
    // IOCP worker thread triggers std::terminate() → entire server process dies.
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
            // Deprecated: old filename-search replaced by AI Search (#26)
            // Forward to aisearch handler for backward compatibility
            STRU_SEARCHFILERQ oldReq = ProtocolFactory::deserializeSearchFileRQ(szbuf + 1, nlen - 1);
            STRU_AISEARCHRQ newReq = {};
            newReq.m_userId = oldReq.m_userId;
            strncpy(newReq.m_szQuery, oldReq.m_szSearchKey, MAXSIZE - 1);
            auto pkt = ProtocolFactory::serializeAISearchRQ(newReq);
            aisearchrq(sock, (const char*)pkt.data(), (int)pkt.size());
            break;
        }
        case _default_protocol_replicate_block_rq:
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
        // Log the error and disconnect the offending client.
        // The IOCP worker thread continues — server stays up.
        fprintf(stderr, "[DEALDATA] Exception from client on socket %lld: %s\n",
                (long long)sock, e.what());
        // Disconnect the client that sent the bad packet
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
// 1. REGISTER — parameterized query + SHA-256 password hashing + async DB
// ============================================================================
void tcpkernel::registerrq(SOCKET sock, const char *szbuf, int nlen)
{
    STRU_REGISTERRQ req = ProtocolFactory::deserializeRegisterRQ(szbuf + 1, nlen - 1);

    // F2-1 fix: m_szPassword plaintext field removed; only SHA-256 is sent
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
                // F1-4 fix: check CreateDirectoryA return value, warn on failure
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
// 2. LOGIN — parameterized query + SHA-256 compare (plaintext fallback) + async DB
// ============================================================================
void tcpkernel::loginrq(SOCKET sock, const char *szbuf, int nlen)
{
    STRU_LOGINRQ req = ProtocolFactory::deserializeLoginRQ(szbuf + 1, nlen - 1);

    // F2-1 fix: m_szPassword plaintext field removed; only SHA-256 is sent
    std::string hashedInput(req.m_szPasswordSHA256);

    m_dbWorker.enqueue([this, req, hashedInput, sock]() {
        STRU_LOGINRS sl;
        // F2-2 fix: unified error — don't leak whether user exists or password is wrong
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
// 3. GET FILE LIST — Bug #6 fix: JOIN user_file instead of files.u_id
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

        // Bug #6 fix: JOIN user_file to get files owned by user
        // OLD (wrong): SELECT ... FROM files WHERE u_id=%lld
        // NEW (correct): JOIN user_file ON f.f_id = uf.f_id WHERE uf.u_id = ?
        // F3-5 fix: add LIMIT to prevent unbounded memory consumption
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
        // CRITICAL FIX: send empty response when user has zero files
        if (!sentAny) {
            sg.m_nFileNum = 0;
            auto packet = ProtocolFactory::serializeGetFileListRS(sg);
            m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
        }
    });
}

// ============================================================================
// 4. UPLOAD FILE INFO — SHA-256 resume (local) + MD5 check (async DB)
// ============================================================================
void tcpkernel::uploadfileinforq(SOCKET sock, const char *szbuf, int nlen)
{
    STRU_UPLOADFILEINFORQ req = ProtocolFactory::deserializeUploadFileInfoRQ(szbuf + 1, nlen - 1);

    // ===================================================================
    // Layer 1: SHA-256 based resume/instant upload check (SQLite, local)
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
                FILE* tempFile = fopen(state->tempPath.c_str(), "ab");
                pResume->m_pfile = tempFile;
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
    // Layer 2: Bloom Filter pre-check (L3 in the three-tier funnel)
    // Avoids unnecessary DB round-trips for brand-new files.
    // If Bloom says "definitely not in DB", we skip the MySQL flash-upload
    // query entirely and jump straight to normal upload path.
    // ===================================================================
    bool bloomHit = false;
    if (m_bloomFilter && !fileHash.empty()) {
        bloomHit = m_bloomFilter->mightContain(fileHash);
        if (!bloomHit) {
            // Bloom Filter says "definitely not in DB" — skip MySQL flash check,
            // go straight to normal upload.  This is the ~1μs fast path for new files.
            qDebug() << "[Bloom L3] MISS — skipping MySQL flash check, proceeding to normal upload";
        } else {
            qDebug() << "[Bloom L3] HIT — will verify with MySQL query";
        }
    }

    // ===================================================================
    // Layer 3: MD5-based check in MySQL (async via DbWorker)
    // ===================================================================
    // Capture everything needed inside the lambda by value
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

        // Only run MySQL flash-upload check if Bloom Filter was hit or unavailable.
        // If Bloom says "definitely not", skip the DB query and go straight to normal upload.
        if (bloomHit) {
        // Bug #6 fix: JOIN user_file to check file ownership (files has no u_id)
        // F5-4 fix: hash-only match — same content, different filename → still flash
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
                // Same user — already uploaded (or resume)
                su.m_szResult = _uploadfile_isuploaded;
            } else {
                // Different user — flash upload (秒传)
                su.m_szResult = _uploadfile_flash;

                // Increment reference count
                m_sql->execute(
                    "UPDATE files SET fcount = fcount + 1 WHERE files.f_sha256=?",
                    {sha256});

                // Create user-file mapping
                m_sql->execute(
                    "INSERT INTO user_file(u_id,f_id) VALUES(?,?)",
                    {userId, fileId});

                // Mark as committed in SQLite for future lookups
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

            // Send flash/already-uploaded response
            auto packet = ProtocolFactory::serializeUploadFileInfoRS(su);
            m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
            return;
        }
        } // end if (bloomHit) — Bloom miss falls through to normal upload

        // ===============================================================
        // File NOT found (or Bloom miss) — NORMAL upload
        // ===============================================================
        su.m_szResult = _uploadfile_normal;

        // Create file path (F4-3 fix: path separator between userId and filename)
        char szfilepath[260] = {0};
        sprintf(szfilepath, "%s%lld\\%s", m_szSystemPath, userId, fname.c_str());
        // F4-5 fix: removed dead fopen — data goes to FileStorage, not this empty file
        // Ensure the user directory exists
        std::string userDir = std::string(m_szSystemPath) + std::to_string(userId);
        CreateDirectoryA(userDir.c_str(), nullptr);

        // Insert file metadata into DB (parameterized)
        long long fileid = 0;
        bool inserted = m_sql->execute(
            "INSERT INTO files(f_name,f_size,f_uploadtime,f_sha256,f_path) VALUES(?,?,?,?,?)",
            {fname, fileSize, uploadTime, sha256, std::string(szfilepath)});

        if (inserted) {
            // Get the assigned file ID
            std::list<std::string> idLst;
            m_sql->query(
                "SELECT f_id FROM files WHERE f_sha256=? AND f_name=?",
                {sha256, fname}, 1, idLst);
            if (!idLst.empty()) {
                fileid = atoll(idLst.front().c_str());
                su.m_fileID = fileid;

                // F16-2 fix: redirect check — if this file should live on a peer node,
                // clean up local DB records and tell the client to reconnect.
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
                    return;  // Don't proceed — client will re-upload to correct node
                }

                // Create user-file mapping
                m_sql->execute(
                    "INSERT INTO user_file(u_id,f_id) VALUES(?,?)",
                    {userId, fileid});
            }
        }

        // Create STRU_FILEINFO for tracking upload progress
        STRU_FILEINFO *p = new STRU_FILEINFO;
        p->m_fileid = fileid;
        p->m_filepos = 0;
        p->m_filesize = fileSize;
        p->m_pfile = nullptr;  // F4-5: no empty file — data goes to FileStorage
        p->m_userid = userId;
        strcpy(p->m_szFileSHA256, fileHash.c_str());
        addFileInfoLocked(p);

        // Create SQLite upload state for resume tracking
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
// 5. UPLOAD FILE BLOCK — FileStorage I/O only (no DB), fix sizeof bug
// ============================================================================
void tcpkernel::uploadfileblockrq(SOCKET sock, const char *szbuf, int nlen)
{
    STRU_UPLOADFILEBLOCKRQ req = ProtocolFactory::deserializeUploadFileBlockRQ(szbuf + 1, nlen - 1);

    // Find the file info in the upload list (thread-safe via mutex)
    STRU_FILEINFO* p = findFileInfoLocked(req.m_fileID);
    if (!p) return;

    // Validate block size — prevent heap buffer overread
    int actualBlockSize = (int)req.m_fileblocksize;
    if (actualBlockSize <= 0 || actualBlockSize > MAXFILECONTENT) return;

    // Write block via FileStorage (append-write engine)
    int blockSeq = (int)(p->m_filepos / MAXFILECONTENT);
    int64_t offset = m_storage->writeBlock(req.m_fileID, blockSeq,
                                           req.m_szFileContent,
                                           actualBlockSize);
    if (offset >= 0) {
        p->m_filepos += req.m_fileblocksize;

        // Replicate to peer nodes (async — enqueue and return, no IOCP blocking)
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
                // F16-5 fix: log warning when replication queue is full
                fprintf(stderr, "[ReplicationWorker] WARNING: queue full — block seq=%d for file=%lld not replicated\n",
                        blockSeq, (long long)req.m_fileID);
            }
        }

        // F4-2 fix: incremental SHA-256 computation as blocks arrive
        if (!p->m_sha256Active) {
            CryptoUtil::sha256Init(&p->m_sha256Ctx);
            p->m_sha256Active = true;
        }
        CryptoUtil::sha256Update(&p->m_sha256Ctx,
            reinterpret_cast<const uint8_t*>(req.m_szFileContent),
            static_cast<size_t>(actualBlockSize));

        // Update resume state after each successful block
        if (m_uploadState && strlen(p->m_szFileSHA256) > 0) {
            // F6-5 fix: completedBlocks counts actual successfully written blocks
            int completedBlocks = blockSeq + 1;
            m_uploadState->updateProgress(p->m_szFileSHA256, req.m_userId,
                                          completedBlocks, p->m_filepos);
        }

        // Send UploadFileBlockRS to ack this block
        STRU_UPLOADFILEBLOCKRS blockRs;
        blockRs.m_fileID = req.m_fileID;
        blockRs.m_pos = p->m_filepos;
        blockRs.m_szResult = 1;
        auto blockPacket = ProtocolFactory::serializeUploadFileBlockRS(blockRs);
        m_server->sendData(sock, (const char*)blockPacket.data(), (int)blockPacket.size());

        // Check if upload is complete
        if (p->m_filepos >= p->m_filesize) {
            // F6-2 fix: set "verifying" state before SHA-256 validation
            std::string expectedHash(p->m_szFileSHA256);
            bool hashOk = true;

            if (m_uploadState && strlen(p->m_szFileSHA256) > 0) {
                m_uploadState->setState(p->m_szFileSHA256, req.m_userId, "verifying");
            }

            // F4-2 fix: verify incremental SHA-256 matches expected hash
            if (p->m_sha256Active && !expectedHash.empty()) {
                std::string actualHash = CryptoUtil::sha256FinalHex(&p->m_sha256Ctx);
                hashOk = (actualHash == expectedHash);
            }

            // F4-2 fix: if expectedHash was empty (L2 "definitely new" path →
            // client skipped full SHA-256), derive it from the server-side
            // incremental SHA-256 context that was built up as blocks arrived.
            std::string finalHash = expectedHash;
            if (finalHash.empty() && p->m_sha256Active) {
                finalHash = CryptoUtil::sha256FinalHex(&p->m_sha256Ctx);
                // Backfill the in-memory struct so downstream code sees the real hash
                strncpy(p->m_szFileSHA256, finalHash.c_str(), sizeof(p->m_szFileSHA256) - 1);
                // Create SQLite committed record (was skipped in uploadfileinforq
                // because fileHash was empty at that point)
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
                // Mark as committed for future instant upload detection
                if (m_uploadState && strlen(p->m_szFileSHA256) > 0) {
                    m_uploadState->setState(p->m_szFileSHA256, req.m_userId, "committed");
                }
                // Update Bloom Filter with new file hash for L3 instant upload
                if (m_bloomFilter && strlen(p->m_szFileSHA256) > 0) {
                    m_bloomFilter->insert(std::string(p->m_szFileSHA256));
                }

                // Backfill DB with real SHA-256 if L2 "definitely new" path
                // wrote an empty placeholder earlier in uploadfileinforq.
                if (!finalHash.empty() && finalHash != expectedHash) {
                    m_dbWorker.enqueue([this, fileId = p->m_fileid, finalHash]() {
                        m_sql->execute(
                            "UPDATE files SET f_sha256=? WHERE f_id=?",
                            {finalHash, fileId});
                    });
                }

                // --- Compute + store sparse fingerprint for L2 pre-check ---
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
                    // Store in DB for warmup after restart
                    m_dbWorker.enqueue([this, fileId = p->m_fileid, sparseFp]() {
                        m_sql->execute(
                            "UPDATE files SET f_sparse_sha256=? WHERE f_id=?",
                            {sparseFp, fileId});
                    });
                }

                // F4-5 fix: m_pfile may be nullptr (data goes to FileStorage, not disk file)
                if (p->m_pfile) {
                    fclose(p->m_pfile);
                    p->m_pfile = nullptr;
                }

                // --- Trigger async AI indexing + tagging after upload ---
                int64_t completedFileId = p->m_fileid;
                int64_t completedUserId = p->m_userid;
                m_dbWorker.enqueue([this, completedFileId, completedUserId]() {
                    std::string content;
                    if (m_storage) {
                        content = m_storage->readBlock(completedFileId, 0);
                    }
                    if (!content.empty()) {
                        if (m_aiSearch) {
                            m_aiSearch->indexFile(completedFileId, content);
                        }
                        if (m_aiTag) {
                            std::list<std::string> nameRows;
                            m_sql->query("SELECT f_name FROM files WHERE f_id=?",
                                         {completedFileId}, 1, nameRows);
                            std::string fileName = nameRows.empty() ? "unknown" : nameRows.front();
                            m_aiTag->tagFileAsync(completedFileId, content, fileName,
                                [this, completedFileId](const TagResult& tagResult) {
                                    if (tagResult.success) {
                                        m_dbWorker.enqueue([this, completedFileId, tagResult]() {
                                            m_sql->execute("DELETE FROM file_tags WHERE f_id=?",
                                                           {completedFileId});
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
                                            // F15-3 fix: persist new tag suggestions to tag_pool
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
                // F4-2/F6-2 fix: SHA-256 verification FAILED
                fprintf(stderr, "[UPLOAD] SHA-256 verification FAILED for fileId=%lld hash=%s\n",
                        (long long)p->m_fileid, expectedHash.c_str());
                if (m_uploadState && strlen(p->m_szFileSHA256) > 0) {
                    m_uploadState->setState(p->m_szFileSHA256, req.m_userId, "verification_failed");
                }
                // Clean up corrupted FileStorage blocks
                if (m_storage) {
                    m_storage->deleteFile(p->m_fileid);
                }
                // Clean up DB records for this failed upload
                m_dbWorker.enqueue([this, fileId = p->m_fileid, userId = p->m_userid]() {
                    m_sql->begin();
                    m_sql->execute("DELETE FROM user_file WHERE u_id=? AND f_id=?", {userId, fileId});
                    m_sql->execute("DELETE FROM files WHERE f_id=?", {fileId});
                    m_sql->commit();
                });
            }

            // Erase from list under lock
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
// 5.5 SPARSE FINGERPRINT PRE-CHECK — L2 in three-tier upload funnel
// ============================================================================
// Client sends sparse fingerprint (head 4KB + tail 4KB + file size, SHA-256'd).
// Server checks in-memory set. If "definitely new", client can skip computing
// the full SHA-256 of the entire file (big win for large files).
void tcpkernel::sparsecheckrq(SOCKET sock, const char* szbuf, int nlen)
{
    auto req = ProtocolFactory::deserializeSparseCheckRQ(szbuf + 1, nlen - 1);

    STRU_SPARSECHECKRS rs;
    // Check in-memory set: O(1) lookup with mutex protection
    std::string fp(req.m_szSparseFingerprint);
    {
        std::lock_guard<std::mutex> lock(m_sparseFpMutex);
        if (!fp.empty() && m_sparseFingerprints.count(fp)) {
            rs.m_szResult = 1;  // might exist → client should compute full SHA-256
        } else {
            rs.m_szResult = 0;  // definitely new → skip full SHA-256
        }
    }
    auto pkt = ProtocolFactory::serializeSparseCheckRS(rs);
    m_server->sendData(sock, (const char*)pkt.data(), (int)pkt.size());
}

// ============================================================================
// 6. DOWNLOAD FILE INFO — Bug #6 fix: JOIN user_file, parameterized query
// ============================================================================
void tcpkernel::downloadfileinforq(SOCKET sock, const char *szbuf, int nlen)
{
    auto req = ProtocolFactory::deserializeDownloadFileInfoRQ(szbuf + 1, nlen - 1);

    // --- Redirect Check (only when fileID is known) ---
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
        // F7-2 fix: zero-initialize to avoid sending uninitialized stack memory.
        // NOTE: `= {}` does NOT zero members because this struct has a user-provided
        // default constructor (value-init only calls that ctor, leaving fields unset),
        // so we memset explicitly and then restore m_ntype.
        STRU_DOWNLOADFILEINFORS rs;
        memset(&rs, 0, sizeof(rs));
        rs.m_ntype = static_cast<char>(_default_protocol_downloadfileinfo_rs);
        rs.m_nBlockSize = MAXFILECONTENT;

        // Bug #6 fix: JOIN user_file — files table has no u_id column
        std::list<std::string> lst;
        m_sql->query(
            "SELECT f.f_id, f.f_size, f.f_sha256 FROM files f "
            "JOIN user_file uf ON f.f_id = uf.f_id "
            "WHERE uf.u_id = ? AND f.f_name = ?",
            {userId, fname}, 3, lst);

        if (lst.size() >= 3) {
            rs.m_fileID = atoll(lst.front().c_str()); lst.pop_front();
            rs.m_fileSize = atoll(lst.front().c_str()); lst.pop_front();
            // F7-2: populate SHA-256 so client can verify integrity after download
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
// 7. DOWNLOAD FILE BLOCK — parameterized query for file path lookup
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

        // Authorization: verify this user owns the file
        std::list<std::string> authLst;
        m_sql->query("SELECT 1 FROM user_file WHERE u_id=? AND f_id=?",
            {req.m_userId, req.m_fileID}, 1, authLst);
        if (authLst.empty()) {
            auto packet = ProtocolFactory::serializeDownloadFileBlockRS(rs);
            m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
            return;
        }

        // Read from FileStorage (blocks.dat) instead of legacy file path
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
// 8. DELETE FILE — Bug #6 fix: use user_file JOIN for ownership check
// ============================================================================
void tcpkernel::deletefilerq(SOCKET sock, const char *szbuf, int nlen)
{
    auto req = ProtocolFactory::deserializeDeleteFileRQ(szbuf + 1, nlen - 1);
    int64_t userId = req.m_userId;
    int64_t fileId = req.m_fileID;

    // --- Redirect Check ---
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

        // Bug #6 fix: check ownership via user_file JOIN (not files.u_id)
        std::list<std::string> lst;
        m_sql->query(
            "SELECT uf.f_id FROM user_file uf "
            "JOIN files f ON f.f_id = uf.f_id "
            "WHERE uf.u_id = ? AND uf.f_id = ?",
            {userId, fileId}, 1, lst);

        if (!lst.empty()) {
            // F8-2 fix: wrap delete operations in a transaction for atomicity
            m_sql->begin();

            // F8-3 fix: check return values — only report success if both queries succeed
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

            // --- Orphan cleanup: if fcount reaches 0, clean up all related data ---
            if (ok1 && ok2) {
                std::list<std::string> fcntLst;
                m_sql->query("SELECT fcount FROM files WHERE f_id = ?", {fileId}, 1, fcntLst);
                if (!fcntLst.empty() && atoll(fcntLst.front().c_str()) == 0) {
                    m_sql->begin();
                    // Delete dependent DB rows (CASCADE would do this if FKs were defined)
                    bool cascadeOk = true;
                    cascadeOk = m_sql->execute("DELETE FROM file_embeddings WHERE f_id = ?", {fileId}) && cascadeOk;
                    cascadeOk = m_sql->execute("DELETE FROM file_tags WHERE f_id = ?", {fileId}) && cascadeOk;
                    cascadeOk = m_sql->execute("DELETE FROM ai_previews WHERE f_id = ?", {fileId}) && cascadeOk;
                    cascadeOk = m_sql->execute("DELETE FROM share_links WHERE f_id = ?", {fileId}) && cascadeOk;
                    cascadeOk = m_sql->execute("DELETE FROM files WHERE f_id = ?", {fileId}) && cascadeOk;
                    if (cascadeOk) {
                        m_sql->commit();
                        // Mark blocks in FileStorage as deleted (marks index entries with # prefix)
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
// 9. SHARE FILE — parameterized insert + executeRaw for DDL
// ============================================================================
void tcpkernel::sharefilerq(SOCKET sock, const char *szbuf, int nlen)
{
    auto req = ProtocolFactory::deserializeShareFileRQ(szbuf + 1, nlen - 1);
    int64_t userId = req.m_userId;
    int64_t fileId = req.m_fileID;

    // --- Redirect Check ---
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

        // --- Authorization: verify user owns the file ---
        std::list<std::string> ownLst;
        m_sql->query(
            "SELECT 1 FROM user_file WHERE u_id=? AND f_id=?",
            {userId, fileId}, 1, ownLst);
        if (ownLst.empty()) {
            // User does not own this file — reject
            auto packet = ProtocolFactory::serializeShareFileRS(rs);
            m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
            return;
        }

        // F10-5: duplicate share detection — return existing code if already shared
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

        // Create share_links table if not exists (DDL — use raw execute)
        m_sql->executeRaw(
            "CREATE TABLE IF NOT EXISTS share_links("
            "share_code CHAR(8) PRIMARY KEY,"
            "f_id BIGINT NOT NULL,"
            "u_id BIGINT NOT NULL,"
            "created_at DATETIME DEFAULT NOW(),"
            "expires_at DATETIME NULL)");

        // Generate a unique share code with collision retry.
        // std::random_device is deterministic on MinGW (constant code → the 2nd
        // share collided on the share_code PK), so use a deterministic mix of
        // wall-clock time + fileId + a per-process monotonic counter + retry round.
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
            sprintf(code, "%08x", rnd);
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
// 10. DELETE SHARE / Revocation — F10-4 fix
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

        // Delete the share link if owned by this user
        bool ok = m_sql->execute(
            "DELETE FROM share_links WHERE f_id=? AND u_id=?",
            {fileId, userId});

        rs.m_szResult = ok ? 1 : 0;

        auto packet = ProtocolFactory::serializeDeleteShareRS(rs);
        m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
    });
}

// ============================================================================
// 10. GET FILE / EXTRACT — parameterized queries     (renumbered to 11)
// ============================================================================
void tcpkernel::getfilerq(SOCKET sock, const char *szbuf, int nlen)
{
    auto req = ProtocolFactory::deserializeGetFileRQ(szbuf + 1, nlen - 1);
    int64_t shareFileID = req.m_shareFileID;
    int64_t userId = req.m_userId;

    // Convert m_shareFileID (int64_t carrying hex code) back to hex string
    char shareCode[9];
    sprintf(shareCode, "%08llx", (unsigned long long)shareFileID);
    std::string sc(shareCode);

    m_dbWorker.enqueue([this, sock, sc, userId, shareFileID]() {
        STRU_GETFILERS rs;
        rs.m_pos = 0;
        rs.m_szResult = 0;
        rs.m_fileID = 0;

        // Lookup share code (with expiry enforcement)
        std::list<std::string> lst;
        m_sql->query(
            "SELECT sl.f_id, sl.u_id FROM share_links sl "
            "WHERE sl.share_code=? AND (sl.expires_at IS NULL OR sl.expires_at > NOW())",
            {sc}, 2, lst);

        if (!lst.empty()) {
            int64_t sharedFileId = atoll(lst.front().c_str()); lst.pop_front();
            int64_t sharerUserId = atoll(lst.front().c_str());

            // --- Guard 1: Prevent self-extract ---
            if (sharerUserId == userId) {
                // Return error — cannot extract own shared file
                rs.m_szResult = 0;
                auto packet = ProtocolFactory::serializeGetFileRS(rs);
                m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
                return;
            }

            // --- Guard 2: Prevent duplicate extract ---
            std::list<std::string> dupLst;
            m_sql->query("SELECT 1 FROM user_file WHERE u_id=? AND f_id=?",
                {userId, sharedFileId}, 1, dupLst);
            if (!dupLst.empty()) {
                // Already extracted — return success with existing file info
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

            // Get file info
            std::list<std::string> flst;
            m_sql->query("SELECT f_name,f_size,f_sha256 FROM files WHERE f_id=?",
                {sharedFileId}, 3, flst);

            if (flst.size() >= 3) {
                strcpy(rs.m_fileInfo.m_szFileName, flst.front().c_str()); flst.pop_front();
                rs.m_fileInfo.m_filesize = atoll(flst.front().c_str()); flst.pop_front();
                strcpy(rs.m_szFileSHA256, flst.front().c_str());

                // --- Reference semantics (not copy): reuse the SAME file row ---
                // Create user-file mapping pointing to the shared file
                m_sql->execute(
                    "INSERT INTO user_file(u_id,f_id) VALUES(?,?)",
                    {userId, sharedFileId});

                // Increment reference count on the file
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
// Bloom Filter Warmup — parameterized query
// ============================================================================
void tcpkernel::warmupBloomFilter()
{
    // Run synchronously before DbWorker starts to avoid connection conflict
    std::list<std::string> sha256s;
    m_sql->query("SELECT f_sha256 FROM files", {}, 1, sha256s);

    for (const auto& sha : sha256s) {
        m_bloomFilter->insert(sha);
    }

    printf("BloomFilter warmup: %zu hashes loaded, %zu bits, %zu hash functions\n",
           sha256s.size(), m_bloomFilter->size(), m_bloomFilter->hashCount());
}

// ============================================================================
// Cluster: Handle incoming replication block from peer
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
// AI: Preview handler (Phase 3 Sprint 3.2)
// ============================================================================
// ============================================================================
// Phase 2: HTTP Streaming Token Handler
// ============================================================================
void tcpkernel::streamtokenrq(SOCKET sock, const char* szbuf, int nlen) {
    auto req = ProtocolFactory::deserializeStreamTokenRQ(szbuf + 1, nlen - 1);

    // Determine HTTP port: if file lives on a peer, use that peer's HTTP port
    // so the streaming URL points directly to the node that holds the data
    int httpPort = m_httpPort;
    if (m_nodeMgr && req.m_fileID > 0 && !m_nodeMgr->isLocal(req.m_fileID)) {
        int peerPort = m_nodeMgr->getHttpPortForFile(req.m_fileID);
        if (peerPort > 0) {
            httpPort = peerPort;
            printf("[StreamToken] Redirecting stream for file=%lld to peer HTTP port %d\n",
                   (long long)req.m_fileID, peerPort);
        }
    }

    // Query file info on DB worker thread to get file name
    m_dbWorker.enqueue([this, sock, req, httpPort]() {
        STRU_STREAMTOKENRS rs = {};  // value-initialize all fields to 0
        rs.m_fileID = req.m_fileID;
        rs.m_nHttpPort = httpPort;
        rs.m_szResult = 1;  // default: error

        // Look up file name from MySQL — verify ownership via user_file JOIN
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

            // Generate streaming token with embedded timestamp
            int64_t ts = 0;
            std::string token = m_httpServer->generateToken(req.m_userId, req.m_fileID, ts);
            strncpy(rs.m_szToken, token.c_str(), sizeof(rs.m_szToken) - 1);
            rs.m_szToken[sizeof(rs.m_szToken) - 1] = '\0';
            rs.m_nTimestamp = ts;

            rs.m_szResult = 0;  // success

            printf("[StreamToken] Generated token for user=%lld file=%lld name=%s\n",
                   (long long)req.m_userId, (long long)req.m_fileID, rs.m_szFileName);
        } else {
            printf("[StreamToken] File not found: fileId=%lld\n", (long long)req.m_fileID);
        }

        // Serialize and send via IOCP main thread
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

        // Get file name from DB
        std::list<std::string> lst;
        m_sql->query(
            "SELECT f_name FROM files f JOIN user_file uf ON f.f_id=uf.f_id WHERE f.f_id=? AND uf.u_id=?",
            {static_cast<int64_t>(req.m_fileID), static_cast<int64_t>(req.m_userId)}, 1, lst);

        if (lst.empty()) {
            rs.m_szResult = 1;
            strcpy(rs.m_szSummary, "File not found");
        } else {
            std::string fileName = lst.front();

            // --- L1: Check AI preview cache ---
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

            // --- Read file content (needed for rawContent display, and for AI if cache miss) ---
            std::string content;
            if (m_storage) {
                content = m_storage->readBlock(req.m_fileID, 0);
            }
            // Legacy fallback: try reading from disk path
            if (content.empty()) {
                std::string fpath = std::string(m_szSystemPath) + std::to_string(req.m_userId) + "\\" + fileName;
                // F13-3 fix: check file size before reading to avoid OOM on large files
                std::ifstream ff(fpath, std::ios::binary | std::ios::ate);
                if (ff) {
                    std::streamsize fsize = ff.tellg();
                    const std::streamsize MAX_LEGACY_READ = 10 * 1024 * 1024; // 10 MB limit
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
                // Cache hit — skip AI, just fill filename + raw content
                strncpy(rs.m_szFileName, fileName.c_str(), sizeof(rs.m_szFileName) - 1);
                int rawLen = std::min(static_cast<int>(content.size()), MAXFILECONTENT * 2);
                rs.m_nRawContentLen = rawLen;
                if (rawLen > 0) memcpy(rs.m_szRawContent, content.c_str(), rawLen);
                rs.m_szAIError[0] = '\0';  // no error, cache served
            } else if (!content.empty()) {
                // Cache miss → call AI → store cache
                auto result = m_aiPreview->preview(req.m_fileID, content, fileName);
                rs.m_szResult = result.success ? 0 : 2;
                strncpy(rs.m_szSummary, result.summary.c_str(), sizeof(rs.m_szSummary) - 1);
                strncpy(rs.m_szKeywords, result.keywords.c_str(), sizeof(rs.m_szKeywords) - 1);
                strncpy(rs.m_szKeySentences, result.keySentences.c_str(), sizeof(rs.m_szKeySentences) - 1);
                strncpy(rs.m_szFileType, result.fileType.c_str(), sizeof(rs.m_szFileType) - 1);
                strncpy(rs.m_szFileName, fileName.c_str(), sizeof(rs.m_szFileName) - 1);
                int rawLen = std::min(static_cast<int>(content.size()), MAXFILECONTENT * 2);
                rs.m_nRawContentLen = rawLen;
                if (rawLen > 0) memcpy(rs.m_szRawContent, content.c_str(), rawLen);
                strncpy(rs.m_szAIError, result.errorMsg.c_str(), sizeof(rs.m_szAIError) - 1);

                // Store to cache if AI succeeded (skip on fallback/error)
                if (result.success && result.errorMsg.empty()) {
                    m_sql->execute(
                        "INSERT INTO ai_previews(f_id, summary, keywords, key_sentences, file_type) "
                        "VALUES(?,?,?,?,?) ON DUPLICATE KEY UPDATE "
                        "summary=VALUES(summary), keywords=VALUES(keywords), key_sentences=VALUES(key_sentences), file_type=VALUES(file_type)",
                        {static_cast<int64_t>(req.m_fileID),
                         result.summary, result.keywords,
                         result.keySentences, result.fileType});
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
// AI: Search handler (Phase 3 Sprint 3.3)
// ============================================================================
void tcpkernel::aisearchrq(SOCKET sock, const char* szbuf, int nlen) {
    auto req = ProtocolFactory::deserializeAISearchRQ(szbuf + 1, nlen - 1);

    m_dbWorker.enqueue([this, sock, req]() {
        STRU_AISEARCHRS rs;
        rs.m_nResultNum = 0;
        rs.m_szResult = 1; // default: fallback

        std::string query(req.m_szQuery);

        // F9-1 fix: escape LIKE special characters (%, _, \) before embedding in pattern
        auto escapeLike = [](const std::string& s) -> std::string {
            std::string out;
            for (char c : s) {
                if (c == '%' || c == '_' || c == '\\') out += '\\';
                out += c;
            }
            return out;
        };

        // F9-2 fix: merge AI + LIKE results (not simple OR).
        // Always run LIKE and deduplicate by fileId against AI results.
        bool aiHasResults = false;
        if (APIBridge::instance()->isEnabled()) {
            auto aiRs = m_aiSearch->search(query, req.m_userId);
            if (aiRs.m_szResult == 0 && aiRs.m_nResultNum > 0) {
                rs = aiRs;
                aiHasResults = true;
            }
        }

        // LIKE fallback — always run, merge with AI results (F9-2 fix)
        {
            // Track seen file IDs from AI results for deduplication
            std::set<int64_t> seenFileIds;
            for (int i = 0; i < rs.m_nResultNum; i++) {
                seenFileIds.insert(rs.m_aryResults[i].m_fileInfo.m_fileID);
            }

            std::list<std::string> lst;
            std::string escapedQuery = escapeLike(query);
            std::string likeQuery = "%" + escapedQuery + "%";
            m_sql->query(
                "SELECT f.f_id, f.f_name, f.f_size FROM files f "
                "JOIN user_file uf ON f.f_id=uf.f_id WHERE uf.u_id=? AND f.f_name LIKE ? LIMIT 45",
                {static_cast<int64_t>(req.m_userId), likeQuery}, 3, lst);

            int idx = rs.m_nResultNum;
            while (lst.size() > 0 && idx < MAXSIZE) {
                int64_t fid = atoll(lst.front().c_str()); lst.pop_front();
                std::string fname = lst.front(); lst.pop_front();
                int64_t fsize = atoll(lst.front().c_str()); lst.pop_front();

                // F9-2: skip duplicates already in AI results
                if (seenFileIds.count(fid)) continue;

                rs.m_aryResults[idx].m_fileInfo.m_fileID = fid;
                strncpy(rs.m_aryResults[idx].m_fileInfo.m_szFileName, fname.c_str(), MAXSIZE - 1);
                rs.m_aryResults[idx].m_fileInfo.m_filesize = fsize;
                memset(rs.m_aryResults[idx].m_szFileSHA256, 0, sizeof(rs.m_aryResults[idx].m_szFileSHA256));
                strcpy(rs.m_aryResults[idx].m_szMatchReason, "filename match");
                idx++;
            }
            rs.m_nResultNum = idx;
        }

        auto packet = ProtocolFactory::serializeAISearchRS(rs);
        m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
    });
}

// ============================================================================
// AI: Tag handler (Phase 3 Sprint 3.4)
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

        // Get file info
        std::list<std::string> lst;
        m_sql->query("SELECT f_name FROM files WHERE f_id=?",
            {static_cast<int64_t>(req.m_fileID)}, 1, lst);

        if (!lst.empty()) {
            std::string fileName = lst.front();

            // F15-2 fix: check cache first — return existing tags without AI call
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
            if (content.empty()) {
                // Fallback: tag by extension
                auto pos = fileName.rfind('.');
                std::string ext = (pos != std::string::npos) ? fileName.substr(pos + 1) : "unknown";
                if (rs.m_nTagNum < 15) {
                    strncpy(rs.m_szTags[rs.m_nTagNum], ext.c_str(), MAXSIZE - 1);
                    rs.m_nTagNum++;
                }
            } else {
                auto tagResult = m_aiTag->tagFile(req.m_fileID, content, fileName);
                if (tagResult.success) {
                    // --- Persist tags to file_tags and tag_pool ---
                    // Clear old tags for this file
                    m_sql->execute("DELETE FROM file_tags WHERE f_id=?",
                                   {static_cast<int64_t>(req.m_fileID)});
                    for (const auto& tag : tagResult.tags) {
                        // Cap tag length
                        std::string safeTag = tag.size() > 99 ? tag.substr(0, 99) : tag;
                        // Insert file_tag
                        m_sql->execute(
                            "INSERT IGNORE INTO file_tags(f_id, tag) VALUES(?,?)",
                            {static_cast<int64_t>(req.m_fileID), safeTag});
                        // Update tag_pool: increment use_count, promote to active at threshold
                        m_sql->execute(
                            "INSERT INTO tag_pool(tag, status, use_count, last_used_at) "
                            "VALUES(?,'pending',1,NOW()) ON DUPLICATE KEY UPDATE "
                            "use_count=use_count+1, last_used_at=NOW(), "
                            "status=IF(use_count >= 3, 'active', status)",
                            {safeTag});

                        if (rs.m_nTagNum < 15) {
                            strncpy(rs.m_szTags[rs.m_nTagNum], safeTag.c_str(), MAXSIZE - 1);
                            rs.m_nTagNum++;
                        }
                    }
                    for (const auto& newTag : tagResult.newTagSuggestions) {
                        if (rs.m_nNewTagSuggestions < 5) {
                            strncpy(rs.m_szNewTags[rs.m_nNewTagSuggestions], newTag.c_str(), MAXSIZE - 1);
                            rs.m_nNewTagSuggestions++;
                        }
                    }
                }
            }
        }

        auto packet = ProtocolFactory::serializeAITagRS(rs);
        m_server->sendData(sock, (const char*)packet.data(), (int)packet.size());
    });
}
