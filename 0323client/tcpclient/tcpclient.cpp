#include "tcpclient.h"
#include <cstdint>
#include <vector>

TCPClient::TCPClient(Ikernel*p)
    : m_sockclient(INVALID_SOCKET)
{
    m_bFlagQuit = true;
    m_pkernel=p;
}

TCPClient::~TCPClient()
{
    uninitNetWork();
}

bool TCPClient::initNetWork(const char *szip, short nport)
{
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

    /* Use the MAKEWORD(lowbyte, highbyte) macro declared in Windef.h */
    wVersionRequested = MAKEWORD(2, 2);

    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        printf("WSAStartup failed with error: %d\n", err);
        return false;
    }
    m_bWSAStarted = true;


    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        uninitNetWork("Could not find a usable version of Winsock.dll\n");
        return false;
    }
    else
        printf("The Winsock 2.2 dll was found okay\n");

    m_sockclient = socket(AF_INET,SOCK_STREAM,0);
    if(m_sockclient == INVALID_SOCKET){
        uninitNetWork("socket err\n");
        return false;
    }
    sockaddr_in  addrserver;
    addrserver.sin_family = AF_INET;
    addrserver.sin_addr.S_un.S_addr =inet_addr(szip);
    addrserver.sin_port= htons(nport);
    if(SOCKET_ERROR == connect(m_sockclient,(const struct sockaddr*)&addrserver,sizeof(addrserver))){
        uninitNetWork("connect err\n");
        return false;
    }

    // start recv thread (stored as member for joinable cleanup)
    m_bFlagQuit = true;
    m_recvThread = std::thread(threadRecv,this);
    return true;
}
void TCPClient::threadRecv(TCPClient *pthis)
{
    while(pthis->m_bFlagQuit){
        pthis->recvData();
    }
}

void TCPClient::uninitNetWork(const char *szerr)
{
    printf("%s", szerr);
    // 1. Signal recv thread to stop
    m_bFlagQuit = false;
    // 2. Close socket to unblock any pending recv() call
    if(m_sockclient != INVALID_SOCKET){
        closesocket(m_sockclient);
        m_sockclient = INVALID_SOCKET;
    }
    // 3. Wait for recv thread to exit cleanly
    if(m_recvThread.joinable()){
        m_recvThread.join();
    }
    // 4. Cleanup WSA (guard against double-cleanup from init failure + destructor)
    if(m_bWSAStarted){
        WSACleanup();
        m_bWSAStarted = false;
    }
}

bool TCPClient::sendData(const char *szbuf, int nlen)
{
    if( !szbuf|| nlen <=0 )
        return false;
    // send 4-byte big-endian length prefix (compatible with IOCP server)
    int32_t netLen = htonl(nlen);
    if(send(m_sockclient,(char*)&netLen,sizeof(int32_t),0)<=0)
        return false;
    //������
    if(send(m_sockclient,szbuf,nlen,0)<=0)
        return false;
    return true;
}

void TCPClient::recvData()
{
    //1. receive 4-byte big-endian length prefix
    int32_t netSize;
    int ret = recv(m_sockclient,(char*)&netSize,sizeof(int32_t),0);
    if(ret <= 0) {
        m_bFlagQuit = false;  // connection closed or error — signal thread to exit
        return;
    }
    int nsize = (int)ntohl(netSize);

    // Security: validate packet size before allocating memory
    // Server caps at 1MB; corrupted/malicious header could request gigabytes → bad_alloc crash
    if (nsize <= 0 || nsize > 1048576) {
        m_bFlagQuit = false;
        return;
    }

    int nOriginalSize = nsize;  // save original size before while-loop overwrites it
    std::vector<char> pszbuf(nsize);
    //2.接收包内容
    int offset = 0;
    while(nsize){
        int nreadnum =  recv(m_sockclient,pszbuf.data()+offset,nsize,0); //100m
        if(nreadnum >0){
            offset += nreadnum; //100m
            nsize -= nreadnum; //0m

        } else {
            // connection closed or error — give up
            m_bFlagQuit = false;
            return;
        }
    }

    try {
        m_pkernel->dealdata(pszbuf.data(), nOriginalSize);
    } catch (const std::exception& e) {
        // Protocol deserialization error (corrupted packet, version mismatch, etc.)
        // Log and continue — do NOT let exception escape the recv thread
        fprintf(stderr, "TCPClient: dealdata exception: %s\n", e.what());
    } catch (...) {
        fprintf(stderr, "TCPClient: dealdata unknown exception\n");
    }
}

