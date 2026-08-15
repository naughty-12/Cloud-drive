#include "tcpkernel.h"
#include "tcpclient/tcpclient.h"
#include "Packdef.h"
#include "ProtocolFactory.h"
tcpkernel::tcpkernel(QObject *parent)
    : QObject{parent}
{
    m_pNet=new TCPClient(this);//调用有参构造
}

tcpkernel::~tcpkernel()
{
    delete m_pNet;
}

bool tcpkernel::connectServer(const char *szip, short nport)
{
    if(!m_pNet->initNetWork(szip,nport))
        return false;
    return true;
}

void tcpkernel::disconnectServer(const char *szerr)
{
    m_pNet->uninitNetWork(szerr);
}

bool tcpkernel::sendData(const char *szbuf, int nlen)
{
    if(!m_pNet->sendData(szbuf,nlen))
        return false;
    return true;
}

void tcpkernel::dealdata(const char *szbuf, int nlen)
{
    switch(*szbuf)
    {
    case _default_protocol_register_rs:
    {
        auto rs = ProtocolFactory::deserializeRegisterRS(szbuf + 1, nlen - 1);
        emit signal_register(rs);
        break;
    }
    case _default_protocol_login_rs:
    {
        auto rs = ProtocolFactory::deserializeLoginRS(szbuf + 1, nlen - 1);
        emit signal_loginerrs(rs);
        break;
    }
    case _default_protocol_getfilelist_rs:
    {
        auto rs = ProtocolFactory::deserializeGetFileListRS(szbuf + 1, nlen - 1);
        emit signal_getfilelistrs(rs);
        break;
    }
    case _default_protocol_uploadfileinfo_rs:
    {
        auto rs = ProtocolFactory::deserializeUploadFileInfoRS(szbuf + 1, nlen - 1);
        emit signal_uploadfileinfors(rs);
        break;
    }
    case _default_protocol_downloadfileinfo_rs:
    {
        auto rs = ProtocolFactory::deserializeDownloadFileInfoRS(szbuf + 1, nlen - 1);
        emit signal_downloadinfors(rs);
        break;
    }
    case _default_protocol_downloadfileblock_rs:
    {
        auto rs = ProtocolFactory::deserializeDownloadFileBlockRS(szbuf + 1, nlen - 1);
        emit signal_downloadblockrs(rs);
        break;
    }
    case _default_protocol_deletefile_rs:
    {
        auto rs = ProtocolFactory::deserializeDeleteFileRS(szbuf + 1, nlen - 1);
        emit signal_deleters(rs);
        break;
    }
    case _default_protocol_sharefile_rs:
    {
        auto rs = ProtocolFactory::deserializeShareFileRS(szbuf + 1, nlen - 1);
        emit signal_sharers(rs);
        break;
    }
    case _default_protocol_getfile_rs:
    {
        auto rs = ProtocolFactory::deserializeGetFileRS(szbuf + 1, nlen - 1);
        emit signal_getfilers(rs);
        break;
    }
    case _default_protocol_redirect_rs:
    {
        auto rs = ProtocolFactory::deserializeRedirectRS(szbuf + 1, nlen - 1);
        emit signal_redirect(rs);
        break;
    }
    case _default_protocol_streamtoken_rs:
    {
        auto rs = ProtocolFactory::deserializeStreamTokenRS(szbuf + 1, nlen - 1);
        emit signal_streamtoken(rs);
        break;
    }
    case _default_protocol_aipreview_rs:
    {
        auto rs = ProtocolFactory::deserializeAIPreviewRS(szbuf + 1, nlen - 1);
        emit signal_aipreview(rs);
        break;
    }
    case _default_protocol_aisearch_rs:
    {
        auto rs = ProtocolFactory::deserializeAISearchRS(szbuf + 1, nlen - 1);
        emit signal_aisearch(rs);
        break;
    }
    case _default_protocol_uploadfileblock_rs:
    {
        // X3 fix: emit block ACK so Widget can count received blocks
        auto rs = ProtocolFactory::deserializeUploadFileBlockRS(szbuf + 1, nlen - 1);
        emit signal_uploadfileblockrs(rs);
        break;
    }
    case _default_protocol_deleteshare_rs:
    {
        // F10-4: share revocation response
        auto rs = ProtocolFactory::deserializeDeleteShareRS(szbuf + 1, nlen - 1);
        emit signal_deletesharers(rs);
        break;
    }
    case _default_protocol_searchfile_rs:
    {
        // Deprecated: replaced by AI Search (#26/#27).
        // Emit via aisearch signal with fallback result.
        auto rs = ProtocolFactory::deserializeSearchFileRS(szbuf + 1, nlen - 1);
        STRU_AISEARCHRS aiRs = {};
        aiRs.m_nResultNum = rs.m_nFileNum;
        for (int i = 0; i < rs.m_nFileNum && i < MAXSIZE; ++i) {
            aiRs.m_aryResults[i].m_fileInfo = rs.m_aryFileInfo[i];
            strcpy(aiRs.m_aryResults[i].m_szMatchReason, "文件名匹配(fallback)");
        }
        emit signal_aisearch(aiRs);
        break;
    }
    case _default_protocol_replicate_block_rs:
    {
        // Peer-to-peer replication ACK — consumed server-side by NodeManager.
        // Silently drop on client (should never arrive here).
        break;
    }
    case _default_protocol_aitag_rs:
    {
        auto rs = ProtocolFactory::deserializeAITagRS(szbuf + 1, nlen - 1);
        emit signal_aitag(rs);
        break;
    }
    case _default_protocol_sparsecheck_rs:
    {
        auto rs = ProtocolFactory::deserializeSparseCheckRS(szbuf + 1, nlen - 1);
        emit signal_sparsecheckrs(rs);
        break;
    }
    }
}



