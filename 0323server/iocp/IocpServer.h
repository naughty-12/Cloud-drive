#ifndef IOCPSERVER_H
#define IOCPSERVER_H

/**
 * @file IocpServer.h
 * @brief Windows IOCP (I/O Completion Port) asynchronous TCP server.
 *
 * Replaces the legacy thread-per-client TCPServer model.
 * Architecture: 1 accept thread + N worker threads (configurable, default 4).
 * Each connection gets a per-I/O context (IocpContext) tracking recv state
 * and send queue.  Protocol parsing uses 4-byte big-endian length prefix
 * followed by BinaryStream-serialized payload.
 *
 * Thread safety: CRITICAL_SECTION on context map (m_ctxLock) and per-connection
 * send queue (ctx->sendLock).  handleDisconnect is idempotent.
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
    void disconnectClient(SOCKET sock);  // Force-disconnect a specific client (used by dealData exception handler)

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
