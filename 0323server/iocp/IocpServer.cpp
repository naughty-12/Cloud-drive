#include "IocpServer.h"
#include <stdio.h>
#include <string.h>
#include <algorithm>

// WinSock2 必须先于 windows.h 包含（已通过 IocpContext.h 完成）

IocpServer::IocpServer()
    : m_iocp(nullptr)
    , m_listenSocket(INVALID_SOCKET)
    , m_running(false)
    , m_workerCount(0)
    , m_workers(nullptr)
{
    InitializeCriticalSection(&m_ctxLock);
}

IocpServer::~IocpServer()
{
    stop();
    DeleteCriticalSection(&m_ctxLock);
}

bool IocpServer::start(const char* ip, short port, int workerCount)
{
    // --- WinSock 初始化 ---
    WSADATA wsaData;
    int err = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (err != 0) {
        printf("IocpServer: WSAStartup failed with error %d\n", err);
        return false;
    }

    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        printf("IocpServer: Winsock 2.2 not available\n");
        WSACleanup();
        return false;
    }

    // --- 创建监听 socket ---
    m_listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenSocket == INVALID_SOCKET) {
        printf("IocpServer: socket() failed, error %d\n", WSAGetLastError());
        WSACleanup();
        return false;
    }

    sockaddr_in addrServer;
    memset(&addrServer, 0, sizeof(addrServer));
    addrServer.sin_family = AF_INET;
    addrServer.sin_addr.s_addr = inet_addr(ip);
    addrServer.sin_port = htons(port);

    if (bind(m_listenSocket, (const sockaddr*)&addrServer, sizeof(addrServer)) == SOCKET_ERROR) {
        printf("IocpServer: bind() failed, error %d\n", WSAGetLastError());
        closesocket(m_listenSocket);
        WSACleanup();
        return false;
    }

    if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        printf("IocpServer: listen() failed, error %d\n", WSAGetLastError());
        closesocket(m_listenSocket);
        WSACleanup();
        return false;
    }

    // --- 创建 IOCP ---
    m_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, (DWORD)workerCount);
    if (m_iocp == nullptr) {
        printf("IocpServer: CreateIoCompletionPort failed, error %d\n", (int)GetLastError());
        closesocket(m_listenSocket);
        WSACleanup();
        return false;
    }

    // --- 启动工作线程 ---
    m_workerCount = workerCount;
    m_workers = new HANDLE[m_workerCount];
    m_running = true;

    for (int i = 0; i < m_workerCount; ++i) {
        m_workers[i] = CreateThread(nullptr, 0, workerThread, this, 0, nullptr);
        if (m_workers[i] == nullptr) {
            printf("IocpServer: CreateThread(worker) failed\n");
            m_running = false;
            // 关闭已创建的工作线程
            for (int j = 0; j < i; ++j) {
                PostQueuedCompletionStatus(m_iocp, 0, 0, nullptr);
            }
            WaitForMultipleObjects(i, m_workers, TRUE, 5000);
            for (int j = 0; j < i; ++j) {
                CloseHandle(m_workers[j]);
            }
            delete[] m_workers;
            m_workers = nullptr;
            CloseHandle(m_iocp);
            closesocket(m_listenSocket);
            WSACleanup();
            return false;
        }
    }

    // --- 启动 accept 线程 ---
    HANDLE hAccept = CreateThread(nullptr, 0, acceptThread, this, 0, nullptr);
    if (hAccept == nullptr) {
        printf("IocpServer: CreateThread(accept) failed\n");
        stop();
        return false;
    }
    CloseHandle(hAccept);  // 无需 join 该线程；stop() 负责清理

    printf("IocpServer: started on %s:%d with %d workers\n", ip, (int)port, m_workerCount);
    return true;
}

void IocpServer::stop()
{
    if (!m_running)
        return;

    m_running = false;

    // 关闭监听 socket，停止接受新连接
    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }

    // 关闭所有客户端 socket（将上下文标记为已断开，
    // 使错误完成到达时 handleDisconnect 变为空操作）
    EnterCriticalSection(&m_ctxLock);
    for (auto& pair : m_contexts) {
        IocpContext* ctx = pair.second;
        ctx->connected = false;
        closesocket(ctx->socket);
        ctx->socket = INVALID_SOCKET;
    }
    LeaveCriticalSection(&m_ctxLock);

    // 唤醒工作线程
    if (m_iocp != nullptr && m_workers != nullptr) {
        for (int i = 0; i < m_workerCount; ++i) {
            PostQueuedCompletionStatus(m_iocp, 0, 0, nullptr);
        }

        // 等待工作线程退出
        WaitForMultipleObjects(m_workerCount, m_workers, TRUE, 5000);

        for (int i = 0; i < m_workerCount; ++i) {
            CloseHandle(m_workers[i]);
        }
    }

    // 清理剩余上下文（正常运行时未被 handleDisconnect
    // 删除的那些）
    EnterCriticalSection(&m_ctxLock);
    for (auto& pair : m_contexts) {
        delete pair.second;
    }
    m_contexts.clear();
    LeaveCriticalSection(&m_ctxLock);

    // 清理 IOCP
    if (m_iocp != nullptr) {
        CloseHandle(m_iocp);
        m_iocp = nullptr;
    }

    // 清理工作线程数组
    if (m_workers != nullptr) {
        delete[] m_workers;
        m_workers = nullptr;
    }

    WSACleanup();
    printf("IocpServer: stopped\n");
}

DWORD WINAPI IocpServer::acceptThread(LPVOID param)
{
    IocpServer* self = (IocpServer*)param;

    while (self->m_running) {
        sockaddr_in addrClient;
        int addrLen = sizeof(addrClient);

        SOCKET clientSock = accept(self->m_listenSocket, (sockaddr*)&addrClient, &addrLen);
        if (clientSock == INVALID_SOCKET) {
            // socket 已关闭（调用了 stop）或发生错误
            if (self->m_running) {
                printf("IocpServer: accept() failed, error %d\n", WSAGetLastError());
            }
            break;
        }

        printf("IocpServer: client connected from %s:%d\n",
               inet_ntoa(addrClient.sin_addr), ntohs(addrClient.sin_port));

        // 创建上下文
        IocpContext* ctx = new IocpContext(clientSock, addrClient);

        // 存入上下文表
        EnterCriticalSection(&self->m_ctxLock);
        self->m_contexts[clientSock] = ctx;
        LeaveCriticalSection(&self->m_ctxLock);

        // 与 IOCP 关联
        if (CreateIoCompletionPort((HANDLE)clientSock, self->m_iocp,
                                   (ULONG_PTR)ctx, 0) == nullptr) {
            printf("IocpServer: CreateIoCompletionPort(client) failed, error %d\n",
                   (int)GetLastError());
            self->handleDisconnect(ctx);
            continue;
        }

        // 发起首次接收
        IoOverlapped* ov = new IoOverlapped;
        memset(&ov->overlapped, 0, sizeof(ov->overlapped));
        ov->opType = IoOpType::Recv;
        ov->wsabuf.buf = ov->buffer;
        ov->wsabuf.len = sizeof(ov->buffer);

        DWORD flags = 0;
        int ret = WSARecv(clientSock, &ov->wsabuf, 1, nullptr, &flags,
                          &ov->overlapped, nullptr);
        if (ret == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING) {
                printf("IocpServer: initial WSARecv failed, error %d\n", err);
                delete ov;
                self->handleDisconnect(ctx);
            }
        }
    }

    return 0;
}

DWORD WINAPI IocpServer::workerThread(LPVOID param)
{
    IocpServer* self = (IocpServer*)param;

    while (self->m_running) {
        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        OVERLAPPED* pOverlapped = nullptr;

        BOOL ok = GetQueuedCompletionStatus(self->m_iocp, &bytesTransferred,
                                            &completionKey, &pOverlapped, INFINITE);

        // 停止信号：PostQueuedCompletionStatus(nullptr, 0, 0, nullptr)
        if (completionKey == 0 && pOverlapped == nullptr) {
            break;
        }

        // 取出上下文与 overlapped
        IocpContext* ctx = (IocpContext*)completionKey;
        IoOverlapped* ov = CONTAINING_RECORD(pOverlapped, IoOverlapped, overlapped);

        // 检查客户端断开或错误
        if (!ok || bytesTransferred == 0) {
            delete ov;
            self->handleDisconnect(ctx);
            continue;
        }

        // 按操作类型分发
        switch (ov->opType) {
        case IoOpType::Recv:
            self->handleRecv(ctx, ov, bytesTransferred);
            break;
        case IoOpType::Send:
            self->handleSend(ctx, ov, bytesTransferred);
            break;
        }
    }

    return 0;
}

void IocpServer::handleRecv(IocpContext* ctx, IoOverlapped* ov, DWORD bytesTransferred)
{
    // 将收到的数据追加到上下文缓冲区
    const char* data = ov->buffer;
    int remaining = (int)bytesTransferred;

    while (remaining > 0) {
        if (ctx->readingHeader) {
            // 头部需 4 字节；ctx->recvBuffer 暂存不完整的头部字节
            int headerNeeded = 4 - (int)ctx->recvBuffer.size();
            int toCopy = (remaining < headerNeeded) ? remaining : headerNeeded;

            ctx->recvBuffer.insert(ctx->recvBuffer.end(), data, data + toCopy);
            data += toCopy;
            remaining -= toCopy;

            if ((int)ctx->recvBuffer.size() == 4) {
                // 提取大端长度
                int32_t netLen = 0;
                memcpy(&netLen, &ctx->recvBuffer[0], 4);
                ctx->expectedLen = (int)ntohl(netLen);
                ctx->recvBuffer.clear();
                ctx->readingHeader = false;

                // 限制在合理上限（1 MB）内，防止无界分配
                if (ctx->expectedLen <= 0 || ctx->expectedLen > 1048576) {
                    printf("IocpServer: invalid packet length %d, disconnecting\n",
                           ctx->expectedLen);
                    delete ov;
                    handleDisconnect(ctx);
                    return;
                }
            }
        } else {
            // 正在读取载荷正文
            int bodyNeeded = ctx->expectedLen - (int)ctx->recvBuffer.size();
            int toCopy = (remaining < bodyNeeded) ? remaining : bodyNeeded;

            ctx->recvBuffer.insert(ctx->recvBuffer.end(), data, data + toCopy);
            data += toCopy;
            remaining -= toCopy;

            if ((int)ctx->recvBuffer.size() == ctx->expectedLen) {
                // 已收到完整数据包
                parsePacket(ctx, &ctx->recvBuffer[0], ctx->expectedLen);
                ctx->resetRecv();
            }
        }
    }

    // 复用同一 IoOverlapped 发起下一次 WSARecv
    memset(&ov->overlapped, 0, sizeof(ov->overlapped));
    ov->wsabuf.buf = ov->buffer;
    ov->wsabuf.len = sizeof(ov->buffer);

    DWORD flags = 0;
    int ret = WSARecv(ctx->socket, &ov->wsabuf, 1, nullptr, &flags,
                      &ov->overlapped, nullptr);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            delete ov;
            handleDisconnect(ctx);
        }
    }
}

void IocpServer::handleSend(IocpContext* ctx, IoOverlapped* ov, DWORD /*bytesTransferred*/)
{
    delete ov;

    // 处理发送队列中的下一帧
    EnterCriticalSection(&ctx->sendLock);

    if (ctx->sendQueue.empty()) {
        ctx->sendInProgress = false;
        LeaveCriticalSection(&ctx->sendLock);
        return;
    }

    std::vector<char> frame = std::move(ctx->sendQueue.front());
    ctx->sendQueue.erase(ctx->sendQueue.begin());
    LeaveCriticalSection(&ctx->sendLock);

    // 为本次发送创建新的 overlapped
    IoOverlapped* newOv = new IoOverlapped;
    memset(&newOv->overlapped, 0, sizeof(newOv->overlapped));
    newOv->opType = IoOpType::Send;

    int toSend = std::min((int)frame.size(), (int)sizeof(newOv->buffer));
    memcpy(newOv->buffer, &frame[0], toSend);
    newOv->wsabuf.buf = newOv->buffer;
    newOv->wsabuf.len = toSend;

    // 若帧超出单个缓冲区大小，将剩余部分重新入队
    if ((int)frame.size() > (int)sizeof(newOv->buffer)) {
        EnterCriticalSection(&ctx->sendLock);
        std::vector<char> remainder(frame.begin() + sizeof(newOv->buffer), frame.end());
        ctx->sendQueue.insert(ctx->sendQueue.begin(), std::move(remainder));
        LeaveCriticalSection(&ctx->sendLock);
    }

    int ret = WSASend(ctx->socket, &newOv->wsabuf, 1, nullptr, 0,
                      &newOv->overlapped, nullptr);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            delete newOv;
            handleDisconnect(ctx);
        }
    }
}

bool IocpServer::sendData(SOCKET sock, const char* data, int len)
{
    if (sock == INVALID_SOCKET || data == nullptr || len <= 0)
        return false;

    // 构造完整帧：[4 字节大端载荷长度][载荷]
    int totalLen = 4 + len;
    std::vector<char> frame(totalLen);
    int32_t netLen = htonl(len);
    memcpy(&frame[0], &netLen, 4);
    memcpy(&frame[4], data, len);

    // 查找上下文
    EnterCriticalSection(&m_ctxLock);
    auto it = m_contexts.find(sock);
    if (it == m_contexts.end()) {
        LeaveCriticalSection(&m_ctxLock);
        return false;
    }
    IocpContext* ctx = it->second;
    LeaveCriticalSection(&m_ctxLock);

    // 检查是否已有发送正在进行
    EnterCriticalSection(&ctx->sendLock);
    if (ctx->sendInProgress) {
        ctx->sendQueue.push_back(std::move(frame));
        LeaveCriticalSection(&ctx->sendLock);
        return true;
    }
    ctx->sendInProgress = true;
    LeaveCriticalSection(&ctx->sendLock);

    // 立即发送
    IoOverlapped* ov = new IoOverlapped;
    memset(&ov->overlapped, 0, sizeof(ov->overlapped));
    ov->opType = IoOpType::Send;

    int toSend = std::min((int)frame.size(), (int)sizeof(ov->buffer));
    memcpy(ov->buffer, &frame[0], toSend);
    ov->wsabuf.buf = ov->buffer;
    ov->wsabuf.len = toSend;

    // 若帧无法放入单个缓冲区，将剩余部分入队
    if ((int)frame.size() > (int)sizeof(ov->buffer)) {
        EnterCriticalSection(&ctx->sendLock);
        std::vector<char> remainder(frame.begin() + sizeof(ov->buffer), frame.end());
        ctx->sendQueue.insert(ctx->sendQueue.begin(), std::move(remainder));
        LeaveCriticalSection(&ctx->sendLock);
    }

    int ret = WSASend(sock, &ov->wsabuf, 1, nullptr, 0, &ov->overlapped, nullptr);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            delete ov;

            // 重置发送状态并清空队列
            EnterCriticalSection(&ctx->sendLock);
            ctx->sendInProgress = false;
            ctx->sendQueue.clear();
            LeaveCriticalSection(&ctx->sendLock);

            handleDisconnect(ctx);
            return false;
        }
    }

    return true;
}

void IocpServer::parsePacket(IocpContext* ctx, const char* data, int len)
{
    if (m_dataCallback) {
        m_dataCallback(ctx->socket, data, len);
    }
}

void IocpServer::handleDisconnect(IocpContext* ctx)
{
    if (!ctx->connected)
        return;

    ctx->connected = false;

    printf("IocpServer: client %s:%d disconnected\n",
           inet_ntoa(ctx->addr.sin_addr), ntohs(ctx->addr.sin_port));

    if (m_disconnectCallback) {
        m_disconnectCallback(ctx->socket);
    }

    // 从上下文表中移除
    EnterCriticalSection(&m_ctxLock);
    m_contexts.erase(ctx->socket);
    LeaveCriticalSection(&m_ctxLock);

    if (ctx->socket != INVALID_SOCKET) {
        closesocket(ctx->socket);
        ctx->socket = INVALID_SOCKET;
    }

    delete ctx;
}

void IocpServer::disconnectClient(SOCKET sock)
{
    // 为外部调用者（如 dealData 异常处理器）提供线程安全的查找与断开
    EnterCriticalSection(&m_ctxLock);
    auto it = m_contexts.find(sock);
    IocpContext* ctx = (it != m_contexts.end()) ? it->second : nullptr;
    LeaveCriticalSection(&m_ctxLock);

    if (ctx) {
        handleDisconnect(ctx);
    }
}
