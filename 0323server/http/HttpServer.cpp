#include "HttpServer.h"
#include "../storage/FileStorage.h"
#include "CryptoUtil.h"          // shared/crypto 共享模块

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstdlib>

// ============================================================================
// 构造函数 / 析构函数
// ============================================================================
// 凭证盐值与会话参数已移到 StreamAccessController（见该类头文件说明）
HttpServer::HttpServer()
    : m_storage(nullptr)
    , m_listenSocket(INVALID_SOCKET)
    , m_port(0)
    , m_running(false)
    , m_sendTimeoutMs(10000)   // 10 秒：慢客户端最多拖住事件循环 10 秒
    , m_recvTimeoutMs(10000)
{
}

HttpServer::~HttpServer() {
    stop();
}

// ============================================================================
// 启动 / 停止
// ============================================================================
bool HttpServer::start(int port, FileStorage* storage) {
    if (m_running) return false;
    if (!storage) return false;

    m_storage = storage;
    m_port = port;

    // 创建 socket
    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) {
        printf("[HttpServer] ERROR: socket() failed, err=%d\n", WSAGetLastError());
        return false;
    }

    // 设置 SO_REUSEADDR，避免重启时出现“地址被占用”
    int reuse = 1;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    // 绑定
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((u_short)port);

    if (bind(m_listenSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("[HttpServer] ERROR: bind() port %d failed, err=%d\n", port, WSAGetLastError());
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        return false;
    }

    // 监听
    if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        printf("[HttpServer] ERROR: listen() failed, err=%d\n", WSAGetLastError());
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        return false;
    }

    // 启动后台线程
    m_running = true;
    m_thread = std::thread(&HttpServer::run, this);

    printf("[HttpServer] Started on port %d (streaming enabled)\n", port);
    return true;
}

void HttpServer::stop() {
    if (!m_running) return;

    m_running = false;

    // 关闭监听 socket 以解除 select() 阻塞
    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    printf("[HttpServer] Stopped.\n");
}

// ============================================================================
// Token 管理（委托 StreamAccessController）
// ============================================================================
std::string HttpServer::generateToken(int64_t userId, int64_t fileId, int64_t& outTs) {
    // 无状态签发，可由 DB worker 线程安全调用
    return m_access.issueToken(userId, fileId, outTs);
}

// ============================================================================
// 客户端 socket 配置（收发超时）
// ============================================================================
void HttpServer::configureClientSocket(SOCKET client) {
    // 发送超时：这是防止"一个慢客户端卡死整个 select 循环"的关键。
    // 播放器暂停/缓冲时不读数据 → 内核发送缓冲区写满 → 没有超时的 send()
    // 会永久阻塞，而事件循环此刻正停在这个调用上，所有人都别想再拿到数据。
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
               (const char*)&m_sendTimeoutMs, sizeof(m_sendTimeoutMs));

    // 接收超时：防止半截请求（只发一半请求头就不发的客户端）占住读循环
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
               (const char*)&m_recvTimeoutMs, sizeof(m_recvTimeoutMs));

    // 开 Nagle 关闭：流媒体是按 Range 小批量发送，攒包会引入额外延迟
    // （失败不影响功能，忽略返回值）
    int nodelay = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY,
               (const char*)&nodelay, sizeof(nodelay));
}

std::string HttpServer::peerIpString(SOCKET client) {
    sockaddr_in addr;
    int len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));

    if (getpeername(client, (sockaddr*)&addr, &len) != 0) {
        return "unknown";
    }

    // 用 inet_ntoa 而不是 inet_ntop：MinGW 下 inet_ntop 需要 _WIN32_WINNT>=0x0600，
    // 而这里只做本机/局域网 IPv4 场景。inet_ntoa 返回静态缓冲，单线程 + 立即拷贝即可。
    const char* ip = inet_ntoa(addr.sin_addr);
    return ip ? std::string(ip) : std::string("unknown");
}

// ============================================================================
// MIME 类型映射
// ============================================================================
std::string HttpServer::getMimeType(const std::string& fileName) {
    // 提取扩展名
    size_t dotPos = fileName.rfind('.');
    if (dotPos == std::string::npos) return "application/octet-stream";

    std::string ext = fileName.substr(dotPos);
    // 转换为小写
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // 视频
    if (ext == ".mp4")  return "video/mp4";
    if (ext == ".mkv")  return "video/x-matroska";
    if (ext == ".avi")  return "video/x-msvideo";
    if (ext == ".mov")  return "video/quicktime";
    if (ext == ".wmv")  return "video/x-ms-wmv";
    if (ext == ".flv")  return "video/x-flv";
    if (ext == ".webm") return "video/webm";
    if (ext == ".m4v")  return "video/x-m4v";
    if (ext == ".3gp")  return "video/3gpp";

    // 音频
    if (ext == ".mp3")  return "audio/mpeg";
    if (ext == ".wav")  return "audio/wav";
    if (ext == ".flac") return "audio/flac";
    if (ext == ".aac")  return "audio/aac";
    if (ext == ".ogg")  return "audio/ogg";
    if (ext == ".wma")  return "audio/x-ms-wma";
    if (ext == ".m4a")  return "audio/mp4";
    if (ext == ".opus") return "audio/opus";

    // 图片
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png")  return "image/png";
    if (ext == ".gif")  return "image/gif";
    if (ext == ".bmp")  return "image/bmp";
    if (ext == ".webp") return "image/webp";
    if (ext == ".svg")  return "image/svg+xml";
    if (ext == ".ico")  return "image/x-icon";
    if (ext == ".tiff" || ext == ".tif") return "image/tiff";

    // 文档
    if (ext == ".pdf")  return "application/pdf";

    // 文本
    if (ext == ".txt")  return "text/plain; charset=utf-8";
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css")  return "text/css; charset=utf-8";
    if (ext == ".js")   return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".xml")  return "application/xml; charset=utf-8";
    if (ext == ".md")   return "text/markdown; charset=utf-8";

    // 压缩包
    if (ext == ".zip")  return "application/zip";
    if (ext == ".rar")  return "application/x-rar-compressed";
    if (ext == ".7z")   return "application/x-7z-compressed";
    if (ext == ".tar")  return "application/x-tar";
    if (ext == ".gz")   return "application/gzip";

    return "application/octet-stream";
}

// ============================================================================
// 服务器主循环（基于 select 的 I/O 多路复用）
// ============================================================================
void HttpServer::run() {
    std::vector<SOCKET> clients;

    while (m_running) {
        // 构建 fd_set
        fd_set readSet;
        FD_ZERO(&readSet);

        if (m_listenSocket != INVALID_SOCKET) {
            FD_SET(m_listenSocket, &readSet);
        }

        SOCKET maxFd = m_listenSocket;
        for (SOCKET client : clients) {
            FD_SET(client, &readSet);
            if (client > maxFd) maxFd = client;
        }

        // 1 秒超时，以便周期性地检查 m_running
        timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select((int)maxFd + 1, &readSet, NULL, NULL, &tv);

        if (ret == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEINTR || err == WSAENOTSOCK) {
                // socket 已被 stop() 关闭
                break;
            }
            continue;  // 瞬时错误，重试
        }
        if (ret == 0) {
            // 超时 tick：顺手清理空闲过期的播放会话（会话表不会随时间无限增长）
            m_access.pruneExpiredSessions((int64_t)time(nullptr));
            continue;
        }

        // 接受新连接
        if (m_listenSocket != INVALID_SOCKET && FD_ISSET(m_listenSocket, &readSet)) {
            SOCKET client = accept(m_listenSocket, NULL, NULL);
            if (client != INVALID_SOCKET) {
                // 每个连接都要设收发超时，否则一个慢客户端能卡死整个循环
                configureClientSocket(client);
                clients.push_back(client);
            }
        }

        // 从已有客户端读取数据
        for (auto it = clients.begin(); it != clients.end(); ) {
            SOCKET client = *it;

            if (FD_ISSET(client, &readSet)) {
                char buf[8192];
                int n = recv(client, buf, sizeof(buf) - 1, 0);

                if (n > 0) {
                    buf[n] = '\0';
                    std::string raw(buf, n);

                    // 检查是否收到了完整的 HTTP 请求
                    // （大多数流媒体 GET 请求可在一个数据包内到达）
                    HttpRequest req = parseRequest(raw);
                    if (!req.method.empty()) {
                        // 来源 IP 交给访问控制器：播放会话要绑定它
                        handleRequest(client, req, peerIpString(client));
                    }
                    // HTTP 是无状态的——处理完一个请求后关闭连接
                    closesocket(client);
                    it = clients.erase(it);
                    continue;
                }
                // n <= 0：客户端断开、超时或出错
                closesocket(client);
                it = clients.erase(it);
                continue;
            }

            // 该客户端暂无数据可读——保持连接存活
            ++it;
        }
    }

    // 关闭时清理剩余客户端
    for (SOCKET client : clients) {
        closesocket(client);
    }
    clients.clear();
}

// ============================================================================
// HTTP 请求解析
// ============================================================================
HttpServer::HttpRequest HttpServer::parseRequest(const std::string& raw) {
    HttpRequest req;
    std::istringstream stream(raw);
    std::string line;

    // 解析请求行：GET /path?query HTTP/1.1
    if (!std::getline(stream, line)) return req;
    // 去除结尾的 \r
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::istringstream lineStream(line);
    lineStream >> req.method;
    std::string fullPath;
    lineStream >> fullPath;

    // 拆分路径与查询字符串
    size_t qPos = fullPath.find('?');
    if (qPos != std::string::npos) {
        req.path = fullPath.substr(0, qPos);
        std::string queryString = fullPath.substr(qPos + 1);

        // 解析查询参数
        std::istringstream qs(queryString);
        std::string param;
        while (std::getline(qs, param, '&')) {
            size_t eqPos = param.find('=');
            if (eqPos != std::string::npos) {
                req.queryParams[param.substr(0, eqPos)] = param.substr(eqPos + 1);
            }
        }
    } else {
        req.path = fullPath;
    }

    // 解析请求头
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;  // 请求头结束

        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string key = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 1);
            // 去除值开头的空格
            if (!value.empty() && value[0] == ' ') value.erase(0, 1);
            req.headers[key] = value;
        }
    }

    return req;
}

// ============================================================================
// 请求处理器
// ============================================================================
void HttpServer::handleRequest(SOCKET client, const HttpRequest& req,
                               const std::string& peerIp) {
    // 仅处理 GET 请求
    if (req.method != "GET") {
        sendError(client, 405, "Method Not Allowed");
        return;
    }

    // 解析路径：/stream/{fileId}
    // 路径格式：/stream/123?token=...&userId=...&ts=...
    const char* STREAM_PREFIX = "/stream/";
    if (req.path.compare(0, strlen(STREAM_PREFIX), STREAM_PREFIX) != 0) {
        // 健康检查端点
        if (req.path == "/health" || req.path == "/") {
            std::string response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Connection: close\r\n"
                "\r\n"
                "0323 Cloud Disk HTTP Server — OK\r\n";
            send(client, response.c_str(), (int)response.size(), 0);
            return;
        }
        sendError(client, 404, "Not Found");
        return;
    }

    // 从路径中提取 fileId
    std::string fileIdStr = req.path.substr(strlen(STREAM_PREFIX));
    int64_t fileId = 0;
    try {
        fileId = std::stoll(fileIdStr);
    } catch (...) {
        sendError(client, 400, "Bad Request: invalid fileId");
        return;
    }

    // 校验 Token
    auto tokenIt = req.queryParams.find("token");
    auto userIdIt = req.queryParams.find("userId");
    auto tsIt = req.queryParams.find("ts");

    if (tokenIt == req.queryParams.end() || userIdIt == req.queryParams.end()
        || tsIt == req.queryParams.end()) {
        sendError(client, 403, "Forbidden: missing token/userId/ts");
        return;
    }

    int64_t userId = 0;
    int64_t ts = 0;
    try {
        userId = std::stoll(userIdIt->second);
        ts = std::stoll(tsIt->second);
    } catch (...) {
        sendError(client, 400, "Bad Request: invalid userId or ts");
        return;
    }

    // 校验访问权限：签发性凭证（60 秒窗口）或已激活的播放会话
    // —— 播放器全程使用同一个 URL，所以"会话"决定了它能否在 60 秒后继续拖进度条
    int64_t now = (int64_t)time(nullptr);
    StreamAccessController::Result access =
        m_access.check(userId, fileId, ts, tokenIt->second, peerIp, now);

    if (access == StreamAccessController::Invalid) {
        printf("[HttpServer] 403 invalid token (user=%lld file=%lld peer=%s)\n",
               (long long)userId, (long long)fileId, peerIp.c_str());
        sendError(client, 403, "Forbidden: invalid token");
        return;
    }
    if (access == StreamAccessController::Expired) {
        printf("[HttpServer] 403 expired token/session (user=%lld file=%lld peer=%s)\n",
               (long long)userId, (long long)fileId, peerIp.c_str());
        sendError(client, 403, "Forbidden: token and playback session expired");
        return;
    }

    // 获取文件大小
    int64_t fileSize = m_storage->getFileSize(fileId);
    if (fileSize <= 0) {
        sendError(client, 404, "File Not Found or empty");
        return;
    }

    // 解析 Range 请求头
    int64_t rangeStart = 0;
    int64_t rangeEnd = fileSize - 1;

    auto rangeIt = req.headers.find("Range");
    if (rangeIt != req.headers.end()) {
        std::string rangeVal = rangeIt->second;
        // 格式："bytes=start-end" 或 "bytes=start-"
        if (rangeVal.compare(0, 6, "bytes=") == 0) {
            std::string rangeSpec = rangeVal.substr(6);
            size_t dashPos = rangeSpec.find('-');

            if (dashPos != std::string::npos) {
                std::string startStr = rangeSpec.substr(0, dashPos);
                std::string endStr = rangeSpec.substr(dashPos + 1);

                // 用 try-catch 包裹 stoll：格式错误的 Range 请求头
                // （如 "bytes=abc-def"）会抛出异常并导致 HTTP 服务器线程崩溃
                try {
                    // F12-6 修复：后缀范围——"bytes=-N" 表示最后 N 个字节
                    if (startStr.empty() && !endStr.empty()) {
                        int64_t suffixLen = std::stoll(endStr);
                        rangeStart = (fileSize > suffixLen) ? (fileSize - suffixLen) : 0;
                        rangeEnd = fileSize - 1;
                    } else {
                        if (!startStr.empty()) {
                            rangeStart = std::stoll(startStr);
                        }
                        if (!endStr.empty()) {
                            rangeEnd = std::stoll(endStr);
                        }
                        if (endStr.empty()) {
                            rangeEnd = fileSize - 1;
                        }
                    }
                } catch (const std::exception&) {
                    sendError(client, 400, "Bad Request — invalid Range format");
                    return;
                }
            }
        }
    }

    // 限制范围（Clamp）
    if (rangeStart < 0) rangeStart = 0;
    if (rangeEnd >= fileSize) rangeEnd = fileSize - 1;
    if (rangeStart > rangeEnd) {
        sendError(client, 416, "Range Not Satisfiable");
        return;
    }

    // 从查询参数中获取文件名（用于 Content-Type）
    std::string fileName;
    auto nameIt = req.queryParams.find("name");
    if (nameIt != req.queryParams.end()) {
        fileName = nameIt->second;
    } else {
        fileName = "file.bin";
    }

    int64_t sentBytes = sendFile(client, fileId, rangeStart, rangeEnd, fileName);

    // 发送量与请求量不符 = 客户端中途断开或触发发送超时（慢客户端被放弃）
    int64_t wanted = rangeEnd - rangeStart + 1;
    if (sentBytes != wanted) {
        printf("[HttpServer] stream aborted: sent %lld/%lld bytes "
               "(file=%lld peer=%s) — slow client or disconnect\n",
               (long long)sentBytes, (long long)wanted,
               (long long)fileId, peerIp.c_str());
    }
}

// ============================================================================
// HTTP 响应辅助函数
// ============================================================================
void HttpServer::sendError(SOCKET client, int code, const std::string& message) {
    const char* codeStr = "";
    switch (code) {
        case 400: codeStr = "Bad Request"; break;
        case 403: codeStr = "Forbidden"; break;
        case 404: codeStr = "Not Found"; break;
        case 405: codeStr = "Method Not Allowed"; break;
        case 416: codeStr = "Range Not Satisfiable"; break;
        case 500: codeStr = "Internal Server Error"; break;
        default:  codeStr = "Error"; break;
    }

    char response[2048];
    int len = snprintf(response, sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/plain\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        code, codeStr, message.c_str());

    send(client, response, len, 0);
}

int64_t HttpServer::sendFile(SOCKET client, int64_t fileId,
                              int64_t rangeStart, int64_t rangeEnd,
                              const std::string& fileName) {
    int64_t fileSize = m_storage->getFileSize(fileId);
    int64_t contentLength = rangeEnd - rangeStart + 1;

    // 构建响应头
    char header[4096];
    int headerLen;

    if (rangeStart == 0 && rangeEnd == fileSize - 1 && fileSize == contentLength) {
        // 完整文件——200 OK（或统一发送 206 以与 Range 支持保持一致）
        headerLen = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %lld\r\n"
            "Accept-Ranges: bytes\r\n"
            "Connection: close\r\n"
            "\r\n",
            getMimeType(fileName).c_str(),
            (long long)contentLength);
    } else {
        // 部分内容——206
        headerLen = snprintf(header, sizeof(header),
            "HTTP/1.1 206 Partial Content\r\n"
            "Content-Type: %s\r\n"
            "Content-Range: bytes %lld-%lld/%lld\r\n"
            "Content-Length: %lld\r\n"
            "Accept-Ranges: bytes\r\n"
            "Connection: close\r\n"
            "\r\n",
            getMimeType(fileName).c_str(),
            (long long)rangeStart, (long long)rangeEnd, (long long)fileSize,
            (long long)contentLength);
    }

    // 发送响应头（此时 socket 已设 SO_SNDTIMEO，最坏情况也会超时返回）
    int sent = send(client, header, headerLen, 0);
    if (sent == SOCKET_ERROR) {
        int err = WSAGetLastError();
        printf("[HttpServer] send header failed: err=%d%s\n", err,
               (err == WSAETIMEDOUT) ? " (send timeout — slow client dropped)" : "");
        return 0;
    }
    if (sent <= 0) return 0;

    // 以 64KB 分块流式发送文件数据
    // 注意：send() 允许"部分发送"（返回值小于请求长度），下面的循环按已发送
    // 字节数推进偏移量，下一轮从断点继续读，所以部分发送也能正确续上。
    const int CHUNK_SIZE = 65536;
    int64_t remaining = contentLength;
    int64_t currentOffset = rangeStart;
    int64_t totalSent = 0;

    while (remaining > 0) {
        int readLen = (int)((std::min)((int64_t)CHUNK_SIZE, remaining));

        std::string data = m_storage->readRange(fileId, currentOffset, readLen);
        if (data.empty()) break;  // 读取错误或 EOF

        int chunkSent = send(client, data.c_str(), (int)data.size(), 0);

        if (chunkSent == SOCKET_ERROR) {
            // WSAETIMEDOUT = 客户端在超时窗口内一直不读数据（暂停/卡死/掉线）。
            // 有 SO_SNDTIMEO 兜底，这里最多等 m_sendTimeoutMs 毫秒就放弃该连接，
            // 而不是让整个 select 循环永久阻塞。
            int err = WSAGetLastError();
            printf("[HttpServer] send failed at offset %lld: err=%d%s\n",
                   (long long)currentOffset, err,
                   (err == WSAETIMEDOUT) ? " (send timeout — slow client dropped)" : "");
            break;
        }
        if (chunkSent == 0) break;  // 正常关闭

        remaining -= chunkSent;
        currentOffset += chunkSent;
        totalSent += chunkSent;
    }

    return totalSent;
}

// ============================================================================
// 工具函数：从 socket 读取以 CRLF 结尾的一行
// ============================================================================
std::string HttpServer::readLine(SOCKET sock) {
    std::string line;
    char ch;
    while (line.size() < 8192) {
        int n = recv(sock, &ch, 1, 0);
        if (n <= 0) return "";
        if (ch == '\r') {
            // 窥探下一个字符是否为 \n
            char next;
            int n2 = recv(sock, &next, 1, MSG_PEEK);
            if (n2 > 0 && next == '\n') {
                recv(sock, &next, 1, 0);  // 消费掉 \n
            }
            return line;
        }
        if (ch == '\n') return line;
        line += ch;
    }
    return line;
}
