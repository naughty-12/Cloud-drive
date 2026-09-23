#ifndef TCPKERNEL_H
#define TCPKERNEL_H

#include "Ikernel.h"
#include "../iocp/IocpServer.h"
#include "Packdef.h"
#include "ProtocolFactory.h"
#include "../db/MySqlWrapper.h"
#include "../db/DbWorker.h"
#include "CryptoUtil.h"          // shared/crypto 共享模块
#include "../storage/FileStorage.h"
#include "../storage/BloomFilter.h"
#include "../db/SqliteState.h"
#include "../cluster/NodeManager.h"
#include "../db/ReplicationWorker.h"
#include "../http/HttpServer.h"
#include "../ai/APIBridge.h"
#include "../ai/AIFilePreview.h"
#include "../ai/AISearchSvc.h"
#include "../ai/AITagService.h"
#include <list>
#include <mutex>
#include <unordered_set>

struct STRU_FILEINFO{
    FILE*m_pfile;
    long long m_filepos;
    long long m_filesize;
    long long m_fileid;
    long long m_userid;
    char m_szFileSHA256[65];  // SHA-256 指纹，用于断点续传状态跟踪
    // F4-2/F6-2 修复：增量 SHA-256 上下文，用于上传完整性校验
    CryptoUtil::Sha256Ctx m_sha256Ctx;
    bool m_sha256Active = false;
    bool m_resumed = false;     // 续传标记：断点/重启后续传时，完成校验按已存块重算完整 SHA-256
};

class tcpkernel : public Ikernel
{
private:
    tcpkernel();
    ~tcpkernel();
public:
    virtual bool  boolopen();
    virtual void close();

    virtual bool dealData(SOCKET sock,const char*szbuf,int nlen);
public:

    static Ikernel*getKernel()
    {

        return m_kernel;
    }
public:
    void registerrq(SOCKET sock, const char* szbuf, int nlen);
    void loginrq(SOCKET sock, const char* szbuf, int nlen);
    void getfilelistrq(SOCKET sock, const char* szbuf, int nlen);
    void uploadfileinforq(SOCKET sock, const char* szbuf, int nlen);
    void uploadfileblockrq(SOCKET sock, const char* szbuf, int nlen);
    void downloadfileinforq(SOCKET sock, const char* szbuf, int nlen);
    void downloadfileblockrq(SOCKET sock, const char* szbuf, int nlen);
    void deletefilerq(SOCKET sock, const char* szbuf, int nlen);
    void sharefilerq(SOCKET sock, const char* szbuf, int nlen);
    void deletesharerq(SOCKET sock, const char* szbuf, int nlen);
    void getfilerq(SOCKET sock, const char* szbuf, int nlen);
    void warmupBloomFilter();
    void recoverUploads();

    // 集群：对等节点复制处理函数
    void replicateblockrq(SOCKET sock, const char* szbuf, int nlen);

    // 阶段二：HTTP 流媒体令牌处理函数
    void streamtokenrq(SOCKET sock, const char* szbuf, int nlen);

    // 阶段三：AI 处理函数
    void aipreviewrq(SOCKET sock, const char* szbuf, int nlen);
    void aisearchrq(SOCKET sock, const char* szbuf, int nlen);
    void aitagrq(SOCKET sock, const char* szbuf, int nlen);

    // L2 稀疏指纹预检处理函数（上传漏斗）
    void sparsecheckrq(SOCKET sock, const char* szbuf, int nlen);

private:
    IocpServer* m_server;
    MySqlWrapper* m_sql;
    DbWorker m_dbWorker;
    ReplicationWorker m_replWorker;
    FileStorage* m_storage;
    BloomFilter* m_bloomFilter;
    std::unordered_set<std::string> m_sparseFingerprints;  // L2：已知稀疏指纹集合，启动时预热加载
    std::mutex m_sparseFpMutex;         // 保护 m_sparseFingerprints（sparsecheckrq 读取，上传完成后写入）
    SqliteState* m_uploadState;
    NodeManager* m_nodeMgr;
    HttpServer* m_httpServer;
    int m_httpPort;  // 来自 server.conf，供 streamtokenrq 使用
    static Ikernel *m_kernel;
    char m_szSystemPath[260];//系统路径
    std::list<STRU_FILEINFO*> m_lstFileInfo;
    std::mutex m_fileInfoMutex;  // 跨 IOCP worker 线程与 DbWorker 线程保护 m_lstFileInfo

    // 阶段三：AI 服务模块
    AIFilePreview* m_aiPreview;
    AISearchSvc*   m_aiSearch;
    AITagService*  m_aiTag;

    // 线程安全辅助函数：按 file_id 查找（调用方不得在解锁后继续持有指针）
    STRU_FILEINFO* findFileInfoLocked(int64_t fileId);
    // 线程安全：添加新的上传会话
    void addFileInfoLocked(STRU_FILEINFO* p);
};

#endif // TCPKERNEL_H
