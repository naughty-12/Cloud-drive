#ifndef TCPKERNEL_H
#define TCPKERNEL_H

#include "Ikernel.h"
#include "../iocp/IocpServer.h"
#include "Packdef.h"
#include "ProtocolFactory.h"
#include "../db/MySqlWrapper.h"
#include "../db/DbWorker.h"
#include "../security/CryptoUtil.h"
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
    char m_szFileSHA256[65];  // SHA-256 fingerprint for resume state tracking
    // F4-2/F6-2 fix: incremental SHA-256 context for upload integrity verification
    CryptoUtil::Sha256Ctx m_sha256Ctx;
    bool m_sha256Active = false;
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

    // Cluster: peer-to-peer replication handler
    void replicateblockrq(SOCKET sock, const char* szbuf, int nlen);

    // Phase 2: HTTP streaming token handler
    void streamtokenrq(SOCKET sock, const char* szbuf, int nlen);

    // Phase 3: AI handlers
    void aipreviewrq(SOCKET sock, const char* szbuf, int nlen);
    void aisearchrq(SOCKET sock, const char* szbuf, int nlen);
    void aitagrq(SOCKET sock, const char* szbuf, int nlen);

    // L2 Sparse Fingerprint pre-check handler (upload funnel)
    void sparsecheckrq(SOCKET sock, const char* szbuf, int nlen);

private:
    IocpServer* m_server;
    MySqlWrapper* m_sql;
    DbWorker m_dbWorker;
    ReplicationWorker m_replWorker;
    FileStorage* m_storage;
    BloomFilter* m_bloomFilter;
    std::unordered_set<std::string> m_sparseFingerprints;  // L2: known sparse fingerprints, warmed up at startup
    std::mutex m_sparseFpMutex;         // protects m_sparseFingerprints (read by sparsecheckrq, written by upload completion)
    SqliteState* m_uploadState;
    NodeManager* m_nodeMgr;
    HttpServer* m_httpServer;
    int m_httpPort;  // from server.conf, used by streamtokenrq
    static Ikernel *m_kernel;
    char m_szSystemPath[260];//系统路径
    std::list<STRU_FILEINFO*> m_lstFileInfo;
    std::mutex m_fileInfoMutex;  // protects m_lstFileInfo across IOCP workers + DbWorker

    // Phase 3: AI service modules
    AIFilePreview* m_aiPreview;
    AISearchSvc*   m_aiSearch;
    AITagService*  m_aiTag;

    // Thread-safe helpers: find by file_id (caller must not hold pointer across unlock)
    STRU_FILEINFO* findFileInfoLocked(int64_t fileId);
    // Thread-safe: add a new upload session
    void addFileInfoLocked(STRU_FILEINFO* p);
};

#endif // TCPKERNEL_H
