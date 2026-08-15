#ifndef TCPCLIENT_H
#define TCPCLIENT_H
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>
#include <atomic>
#include "INet.h"
#include "kernel/Ikernel.h"
class TCPClient : public INet
{
public:
    TCPClient(Ikernel*p);
    ~TCPClient();
public:
    bool initNetWork(const char* szip = "127.0.0.1",short nport = 8899);
    void uninitNetWork(const char* szerr="");
    bool sendData(const char*szbuf,int nlen );
    void recvData();
public:
    static void threadRecv(TCPClient*);
private:
    SOCKET m_sockclient;
    std::atomic<bool> m_bFlagQuit{true};
    bool m_bWSAStarted{false};   // guard against double-WSACleanup
    std::thread m_recvThread;
    Ikernel*m_pkernel;
};

#endif // TCPCLIENT_H
