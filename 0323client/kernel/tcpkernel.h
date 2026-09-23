#ifndef TCPKERNEL_H
#define TCPKERNEL_H

#include <QObject>
#include "IKernel.h"
#include "tcpclient/tcpclient.h"
#include "Packdef.h"
#include "ProtocolFactory.h"
class tcpkernel : public QObject,public Ikernel
{
    Q_OBJECT
public:
    explicit tcpkernel(QObject *parent = nullptr);
    tcpkernel(const tcpkernel &) = delete;
    tcpkernel(tcpkernel &&) = delete;
    tcpkernel &operator=(const tcpkernel &) = delete;
    tcpkernel &operator=(tcpkernel &&) = delete;
    ~tcpkernel();
signals:
    void signal_register(const STRU_REGISTERRS&);
    void signal_loginerrs(const STRU_LOGINRS&);
    void signal_getfilelistrs(const STRU_GETFILELISTRS&);
    void signal_uploadfileinfors(const STRU_UPLOADFILEINFORS&);
    // Phase 2：下载、删除、分享、提取
    void signal_downloadinfors(const STRU_DOWNLOADFILEINFORS&);
    void signal_downloadblockrs(const STRU_DOWNLOADFILEBLOCKRS&);
    void signal_deleters(const STRU_DELETEFILERS&);
    void signal_sharers(const STRU_SHAREFILERS&);
    void signal_getfilers(const STRU_GETFILERS&);
    // 集群：重定向信号
    void signal_redirect(const STRU_REDIRECTRS&);

    // Phase 2：HTTP 流媒体令牌
    void signal_streamtoken(const STRU_STREAMTOKENRS&);

    // 上传块 ACK（X3：发送后即忘修复）
    void signal_uploadfileblockrs(const STRU_UPLOADFILEBLOCKRS&);
    // 删除分享（F10-4：分享撤销）
    void signal_deletesharers(const STRU_DELETESHARERS&);
    // L2 稀疏指纹预检（上传漏斗）
    void signal_sparsecheckrs(const STRU_SPARSECHECKRS&);
    // Phase 3：AI 信号
    void signal_aipreview(const STRU_AIPREVIEWRS&);
    void signal_aisearch(const STRU_AISEARCHRS&);
    void signal_aitag(const STRU_AITAGRS&);
public:
    virtual bool connectServer(const char*szip="127.0.0.1",short nport=8899);
    virtual void disconnectServer(const char*szerr="");
    virtual bool sendData(const char*szbuf,int nlen);
    virtual void dealdata(const char*szbuf, int nlen);
private:
    INet *m_pNet;
};

#endif // TCPKERNEL_H
