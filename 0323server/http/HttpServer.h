#ifndef HTTPSERVER_H
#define HTTPSERVER_H

// FD_SETSIZE 必须在 winsock2.h 之前定义，以突破默认的 64 个 socket 上限
#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif

// WinSock2 必须在 windows.h 之前包含（Qt 可能间接引入 windows.h）
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <map>
#include <atomic>
#include <thread>
#include <cstdint>

#include "StreamAccessController.h"

// 前置声明
class FileStorage;

// ============================================================================
// HttpServer — 轻量级 HTTP/1.1 流媒体服务器
// ============================================================================
// 运行在独立端口（默认 8900），与 IOCP TCP 服务器相互独立。
// 支持：
//   - GET /stream/{fileId}?token=...&userId=...&ts=...  （文件流式传输）
//   - Range 请求头解析 → 206 Partial Content
//   - 两级访问控制（实现见 StreamAccessController）：
//       1) 签发性凭证：SHA-256，60 秒窗口（防伪造、防重放、URL 泄露也快失效）
//       2) 播放会话：首次校验通过后激活，绑定来源 IP，空闲窗口内可反复使用
//          —— 因为播放器拿着固定 URL，无法在播放中途更换凭证
//   - 基于 select() 的 I/O 多路复用（无需每连接一个线程）
//   - 每个客户端 socket 设发送/接收超时，慢客户端不会阻塞整个事件循环
//
// 设计决策：
//   - 零外部依赖（纯 WinSock2 + C++11）
//   - 出于简单性使用 select() 而非 IOCP（连接数较少）；但慢客户端必须靠
//     SO_SNDTIMEO 兜住，否则一次阻塞发送会卡死整个流媒体服务
//   - Token = SHA-256(fileId + userId + timestamp + SECRET_SALT)
// ============================================================================

class HttpServer {
public:
    HttpServer();
    ~HttpServer();

    // 在指定端口启动 HTTP 服务器（后台线程）
    bool start(int port, FileStorage* storage);

    // 停止服务器并关闭所有连接
    void stop();

    // 检查服务器是否在运行
    bool isRunning() const { return m_running; }

    // 获取监听端口
    int port() const { return m_port; }

    // 为指定的用户+文件生成流媒体 Token（委托 StreamAccessController）
    // Token = SHA-256(fileId + userId + timestamp + SECRET_SALT)
    // @param outTs  输出：Token 中使用的 Unix 时间戳（用于 URL）
    std::string generateToken(int64_t userId, int64_t fileId, int64_t& outTs);

    // --- 访问控制 / 超时配置（在 start() 之前设置；不设则用默认值）---

    /// 访问控制器（凭证窗口 + 播放会话表），供 tcpkernel 读取 server.conf 后配置
    StreamAccessController&       access()       { return m_access; }
    const StreamAccessController& access() const { return m_access; }

    /// 单个客户端的发送超时（毫秒）。默认 10000
    /// 关键：send() 没有超时的话，一个不读数据的播放器会让整个 select 循环永久阻塞
    void setSendTimeoutMs(int ms) { m_sendTimeoutMs = ms; }
    int  sendTimeoutMs() const    { return m_sendTimeoutMs; }

    /// 单个客户端的接收超时（毫秒）。默认 10000
    void setRecvTimeoutMs(int ms) { m_recvTimeoutMs = ms; }
    int  recvTimeoutMs() const    { return m_recvTimeoutMs; }

private:
    // 服务器主循环（在后台线程中运行）
    void run();

    // HTTP 请求结构体
    struct HttpRequest {
        std::string method;
        std::string path;
        std::map<std::string, std::string> headers;
        std::map<std::string, std::string> queryParams;
    };

    // 解析原始 HTTP 请求文本
    HttpRequest parseRequest(const std::string& raw);

    // 处理单个 HTTP 请求（peerIp：请求来源 IP，播放会话要绑定它）
    void handleRequest(SOCKET client, const HttpRequest& req, const std::string& peerIp);

    // 发送错误响应
    void sendError(SOCKET client, int code, const std::string& message);

    // 流式发送文件数据（支持 Range）
    // @return 实际发送的字节数；< 0 表示客户端中途断开或超时
    int64_t sendFile(SOCKET client, int64_t fileId, int64_t rangeStart, int64_t rangeEnd,
                     const std::string& fileName);

    // 给新接入的客户端 socket 设置收发超时
    void configureClientSocket(SOCKET client);

    // 取对端 IP 字符串（用于播放会话绑定）
    static std::string peerIpString(SOCKET client);

    // 根据文件扩展名获取 MIME 类型
    static std::string getMimeType(const std::string& fileName);

    // 从 socket 读取一行（以 CRLF 结尾）
    static std::string readLine(SOCKET sock);

    FileStorage* m_storage;
    SOCKET m_listenSocket;
    int m_port;
    std::atomic<bool> m_running;
    std::thread m_thread;

    // 访问控制（凭证 60 秒窗口 + 播放会话，会话绑定来源 IP）
    StreamAccessController m_access;

    // 每客户端收发超时
    int m_sendTimeoutMs;
    int m_recvTimeoutMs;
};

#endif // HTTPSERVER_H
