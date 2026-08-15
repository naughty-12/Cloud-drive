#ifndef IOPCCONTEXT_H
#define IOPCCONTEXT_H

#include <winsock2.h>
#include <windows.h>
#include <vector>
#include <string>

// Per-I/O operation types
enum class IoOpType : int {
    Recv,
    Send
};

// Overlapped extension for IOCP
struct IoOverlapped {
    OVERLAPPED  overlapped;     // Must be first for CONTAINING_RECORD
    IoOpType    opType;
    WSABUF      wsabuf;
    char        buffer[8192];   // I/O buffer (8KB per recv)
};

// Per-connection state
struct IocpContext {
    SOCKET      socket;
    sockaddr_in addr;           // for logging
    bool        connected;

    // Receive state
    std::vector<char> recvBuffer;  // accumulated data for current packet
    int             expectedLen;   // expected packet length (from 4-byte header)
    bool            readingHeader; // true = reading 4-byte length, false = reading payload

    // Send queue (for async send completion)
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
