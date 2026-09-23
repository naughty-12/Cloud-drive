#ifndef IOPCCONTEXT_H
#define IOPCCONTEXT_H

#include <winsock2.h>
#include <windows.h>
#include <vector>
#include <string>

// 每次 I/O 操作的类型
enum class IoOpType : int {
    Recv,
    Send
};

// IOCP 的 Overlapped 扩展
struct IoOverlapped {
    OVERLAPPED  overlapped;     // 必须位于首位，供 CONTAINING_RECORD 使用
    IoOpType    opType;
    WSABUF      wsabuf;
    char        buffer[8192];   // I/O 缓冲区（每次接收 8KB）
};

// 每连接状态
struct IocpContext {
    SOCKET      socket;
    sockaddr_in addr;           // 用于日志记录
    bool        connected;

    // 接收状态
    std::vector<char> recvBuffer;  // 当前数据包累积的数据
    int             expectedLen;   // 期望的数据包长度（来自 4 字节头部）
    bool            readingHeader; // true = 正在读取 4 字节长度，false = 正在读取载荷

    // 发送队列（用于异步发送完成处理）
    CRITICAL_SECTION sendLock;
    std::vector<std::vector<char>> sendQueue;
    bool              sendInProgress;

    IocpContext(SOCKET s, const sockaddr_in& a)
        : socket(s), addr(a), connected(true)
        , expectedLen(0), readingHeader(true), sendInProgress(false)
    {
        InitializeCriticalSection(&sendLock);
    }

    ~IocpContext() {
        DeleteCriticalSection(&sendLock);
    }

    void resetRecv() {
        recvBuffer.clear();
        expectedLen = 0;
        readingHeader = true;
    }
};

#endif // IOPCCONTEXT_H
