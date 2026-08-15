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
    // Phase 2: Download, Delete, Share, Extract
    void signal_downloadinfors(const STRU_DOWNLOADFILEINFORS&);
    void signal_downloadblockrs(const STRU_DOWNLOADFILEBLOCKRS&);
    void signal_deleters(const STRU_DELETEFILERS&);
    void signal_sharers(const STRU_SHAREFILERS&);
    void signal_getfilers(const STRU_GETFILERS&);
    // Cluster: redirect signal
    void signal_redirect(const STRU_REDIRECTRS&);

    // Phase 2: HTTP streaming token
    void signal_streamtoken(const STRU_STREAMTOKENRS&);

    // Upload block ACK (X3: fire-and-forget fix)
    void signal_uploadfileblockrs(const STRU_UPLOADFILEBLOCKRS&);
    // Delete Share (F10-4: share revocation)
    void signal_deletesharers(const STRU_DELETESHARERS&);
    // L2 Sparse fingerprint pre-check (upload funnel)
    void signal_sparsecheckrs(const STRU_SPARSECHECKRS&);
    // Phase 3: AI signals
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
