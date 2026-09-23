#ifndef IOCPSERVER_H
#define IOCPSERVER_H

/**
 * @file IocpServer.h
 * @brief Windows IOCP（I/O 完成端口）异步 TCP 服务器。
 *
 * 取代旧的一连接一线程的 TCPServer 模型。
 * 架构：1 个 accept 线程 + N 个工作线程（可配置，默认 4 个）。
 * 每个连接对应一个 I/O 上下文（IocpContext），跟踪接收状态
 * 与发送队列。协议解析使用 4 字节大端长度前缀，
 * 后跟 BinaryStream 序列化的载荷。
 *
 * 线程安全：上下文表（m_ctxLock）与每连接发送队列（ctx->sendLock）
 * 均使用 CRITICAL_SECTION 保护；handleDisconnect 为幂等操作。
 */

#include <winsock2.h>
#include <windows.h>
#include <functional>
#include <string>
#include <unordered_map>
#include "IocpContext.h"

class IocpServer {
public:
    using DataCallback = std::function<void(SOCKET, const char*, int)>;
    using DisconnectCallback = std::function<void(SOCKET)>;

    IocpServer();
    ~IocpServer();

    void setDataCallback(DataCallback cb) { m_dataCallback = std::move(cb); }
    void setDisconnectCallback(DisconnectCallback cb) { m_disconnectCallback = std::move(cb); }

    bool start(const char* ip = "127.0.0.1", short port = 8899, int workerCount = 4);
    void stop();
    bool sendData(SOCKET sock, const char* data, int len);
    void disconnectClient(SOCKET sock);  // 强制断开指定客户端（供 dealData 异常处理器使用）

private:
    static DWORD WINAPI acceptThread(LPVOID param);
    static DWORD WINAPI workerThread(LPVOID param);
    void handleRecv(IocpContext* ctx, IoOverlapped* ov, DWORD bytesTransferred);
    void handleSend(IocpContext* ctx, IoOverlapped* ov, DWORD bytesTransferred);
    void handleDisconnect(IocpContext* ctx);
    void parsePacket(IocpContext* ctx, const char* data, int len);

    HANDLE      m_iocp;
    SOCKET      m_listenSocket;
    bool        m_running;
    int         m_workerCount;
    HANDLE*     m_workers;

    CRITICAL_SECTION m_ctxLock;
    std::unordered_map<SOCKET, IocpContext*> m_contexts;

    DataCallback       m_dataCallback;
    DisconnectCallback m_disconnectCallback;
};

#endif
