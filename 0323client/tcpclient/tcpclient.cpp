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

    /* 使用 Windef.h 中声明的 MAKEWORD(lowbyte, highbyte) 宏 */
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

    // 启动接收线程（存为成员变量以便 join 清理）
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
    // 1. 通知接收线程停止
    m_bFlagQuit = false;
    // 2. 关闭 socket，解除阻塞中的 recv() 调用
    if(m_sockclient != INVALID_SOCKET){
        closesocket(m_sockclient);
        m_sockclient = INVALID_SOCKET;
    }
    // 3. 等待接收线程干净退出
    if(m_recvThread.joinable()){
        m_recvThread.join();
    }
    // 4. 清理 WSA（防止 init 失败与析构造成重复清理）
    if(m_bWSAStarted){
        WSACleanup();
        m_bWSAStarted = false;
    }
}

bool TCPClient::sendData(const char *szbuf, int nlen)
{
    if( !szbuf|| nlen <=0 )
        return false;
    // 发送 4 字节大端长度前缀（与 IOCP 服务端兼容）
    int32_t netLen = htonl(nlen);
    if(send(m_sockclient,(char*)&netLen,sizeof(int32_t),0)<=0)
        return false;
    //发送数据
    if(send(m_sockclient,szbuf,nlen,0)<=0)
        return false;
    return true;
}

void TCPClient::recvData()
{
    //1. 接收 4 字节大端长度前缀
    int32_t netSize;
    int ret = recv(m_sockclient,(char*)&netSize,sizeof(int32_t),0);
    if(ret <= 0) {
        m_bFlagQuit = false;  // 连接已关闭或出错——通知线程退出
        return;
    }
    int nsize = (int)ntohl(netSize);

    // 安全：分配内存前校验包大小
    // 服务端上限为 1MB；损坏/恶意的包头可能请求数 GB 大小 → bad_alloc 崩溃
    if (nsize <= 0 || nsize > 1048576) {
        m_bFlagQuit = false;
        return;
    }

    int nOriginalSize = nsize;  // 在 while 循环覆盖 nsize 前保存原始大小
    std::vector<char> pszbuf(nsize);
    //2.接收包内容
    int offset = 0;
    while(nsize){
        int nreadnum =  recv(m_sockclient,pszbuf.data()+offset,nsize,0); //100m
        if(nreadnum >0){
            offset += nreadnum; //100m
            nsize -= nreadnum; //0m

        } else {
            // 连接已关闭或出错——放弃
            m_bFlagQuit = false;
            return;
        }
    }

    try {
        m_pkernel->dealdata(pszbuf.data(), nOriginalSize);
    } catch (const std::exception& e) {
        // 协议反序列化错误（包损坏、版本不匹配等）
        // 记录日志并继续——绝不让异常逃逸出接收线程
        fprintf(stderr, "TCPClient: dealdata exception: %s\n", e.what());
    } catch (...) {
        fprintf(stderr, "TCPClient: dealdata unknown exception\n");
    }
}

