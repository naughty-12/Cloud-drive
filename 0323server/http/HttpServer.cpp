#include "HttpServer.h"
#include "../storage/FileStorage.h"
#include "../security/CryptoUtil.h"

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
// Constants
// ============================================================================
const char* HttpServer::SECRET_SALT = "0323CloudDisk_HTTP_STREAM_2026";

// ============================================================================
// Constructor / Destructor
// ============================================================================
HttpServer::HttpServer()
    : m_storage(nullptr)
    , m_listenSocket(INVALID_SOCKET)
    , m_port(0)
    , m_running(false)
{
}

HttpServer::~HttpServer() {
    stop();
}

// ============================================================================
// Start / Stop
// ============================================================================
bool HttpServer::start(int port, FileStorage* storage) {
    if (m_running) return false;
    if (!storage) return false;

    m_storage = storage;
    m_port = port;

    // Create socket
    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) {
        printf("[HttpServer] ERROR: socket() failed, err=%d\n", WSAGetLastError());
        return false;
    }

    // Set SO_REUSEADDR to avoid "address in use" on restart
    int reuse = 1;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    // Bind
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

    // Listen
    if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        printf("[HttpServer] ERROR: listen() failed, err=%d\n", WSAGetLastError());
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        return false;
    }

    // Start background thread
    m_running = true;
    m_thread = std::thread(&HttpServer::run, this);

    printf("[HttpServer] Started on port %d (streaming enabled)\n", port);
    return true;
}

void HttpServer::stop() {
    if (!m_running) return;

    m_running = false;

    // Close listen socket to unblock select()
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
// Token Management
// ============================================================================
std::string HttpServer::generateToken(int64_t userId, int64_t fileId, int64_t& outTs) {
    outTs = (int64_t)time(nullptr);
    std::ostringstream oss;
    oss << fileId << "|" << userId << "|" << outTs << "|" << SECRET_SALT;
    return CryptoUtil::sha256(oss.str());
}

bool HttpServer::validateToken(int64_t fileId, int64_t userId, int64_t ts,
                               const std::string& token) {
    // F12-3 fix: one-sided time window — reject future timestamps and expired tokens
    int64_t now = (int64_t)time(nullptr);
    int64_t diff = now - ts;
    if (diff < 0 || diff > TOKEN_TIMEOUT_SECONDS) return false;

    // Recompute expected token
    std::ostringstream oss;
    oss << fileId << "|" << userId << "|" << ts << "|" << SECRET_SALT;
    std::string expected = CryptoUtil::sha256(oss.str());

    return token == expected;
}

// ============================================================================
// MIME Type Mapping
// ============================================================================
std::string HttpServer::getMimeType(const std::string& fileName) {
    // Extract extension
    size_t dotPos = fileName.rfind('.');
    if (dotPos == std::string::npos) return "application/octet-stream";

    std::string ext = fileName.substr(dotPos);
    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Video
    if (ext == ".mp4")  return "video/mp4";
    if (ext == ".mkv")  return "video/x-matroska";
    if (ext == ".avi")  return "video/x-msvideo";
    if (ext == ".mov")  return "video/quicktime";
    if (ext == ".wmv")  return "video/x-ms-wmv";
    if (ext == ".flv")  return "video/x-flv";
    if (ext == ".webm") return "video/webm";
    if (ext == ".m4v")  return "video/x-m4v";
    if (ext == ".3gp")  return "video/3gpp";

    // Audio
    if (ext == ".mp3")  return "audio/mpeg";
    if (ext == ".wav")  return "audio/wav";
    if (ext == ".flac") return "audio/flac";
    if (ext == ".aac")  return "audio/aac";
    if (ext == ".ogg")  return "audio/ogg";
    if (ext == ".wma")  return "audio/x-ms-wma";
    if (ext == ".m4a")  return "audio/mp4";
    if (ext == ".opus") return "audio/opus";

    // Images
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png")  return "image/png";
    if (ext == ".gif")  return "image/gif";
    if (ext == ".bmp")  return "image/bmp";
    if (ext == ".webp") return "image/webp";
    if (ext == ".svg")  return "image/svg+xml";
    if (ext == ".ico")  return "image/x-icon";
    if (ext == ".tiff" || ext == ".tif") return "image/tiff";

    // Documents
    if (ext == ".pdf")  return "application/pdf";

    // Text
    if (ext == ".txt")  return "text/plain; charset=utf-8";
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css")  return "text/css; charset=utf-8";
    if (ext == ".js")   return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".xml")  return "application/xml; charset=utf-8";
    if (ext == ".md")   return "text/markdown; charset=utf-8";

    // Archives
    if (ext == ".zip")  return "application/zip";
    if (ext == ".rar")  return "application/x-rar-compressed";
    if (ext == ".7z")   return "application/x-7z-compressed";
    if (ext == ".tar")  return "application/x-tar";
    if (ext == ".gz")   return "application/gzip";

    return "application/octet-stream";
}

// ============================================================================
// Main Server Loop (select-based I/O multiplexing)
// ============================================================================
void HttpServer::run() {
    std::vector<SOCKET> clients;

    while (m_running) {
        // Build fd_set
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

        // 1-second timeout to check m_running periodically
        timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select((int)maxFd + 1, &readSet, NULL, NULL, &tv);

        if (ret == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEINTR || err == WSAENOTSOCK) {
                // Socket was closed by stop()
                break;
            }
            continue;  // Transient error, retry
        }
        if (ret == 0) continue;  // Timeout, check m_running

        // Accept new connections
        if (m_listenSocket != INVALID_SOCKET && FD_ISSET(m_listenSocket, &readSet)) {
            SOCKET client = accept(m_listenSocket, NULL, NULL);
            if (client != INVALID_SOCKET) {
                clients.push_back(client);
            }
        }

        // Read from existing clients
        for (auto it = clients.begin(); it != clients.end(); ) {
            SOCKET client = *it;

            if (FD_ISSET(client, &readSet)) {
                char buf[8192];
                int n = recv(client, buf, sizeof(buf) - 1, 0);

                if (n > 0) {
                    buf[n] = '\0';
                    std::string raw(buf, n);

                    // Check if we got the full HTTP request
                    // (most GET requests for streaming fit in one packet)
                    HttpRequest req = parseRequest(raw);
                    if (!req.method.empty()) {
                        handleRequest(client, req);
                    }
                    // HTTP is stateless — close after handling one request
                    closesocket(client);
                    it = clients.erase(it);
                    continue;
                }
                // n <= 0: client disconnected or error
                closesocket(client);
                it = clients.erase(it);
                continue;
            }

            // No data ready for this client yet — keep it alive
            ++it;
        }
    }

    // Cleanup remaining clients on shutdown
    for (SOCKET client : clients) {
        closesocket(client);
    }
    clients.clear();
}

// ============================================================================
// HTTP Request Parsing
// ============================================================================
HttpServer::HttpRequest HttpServer::parseRequest(const std::string& raw) {
    HttpRequest req;
    std::istringstream stream(raw);
    std::string line;

    // Parse request line: GET /path?query HTTP/1.1
    if (!std::getline(stream, line)) return req;
    // Trim trailing \r
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::istringstream lineStream(line);
    lineStream >> req.method;
    std::string fullPath;
    lineStream >> fullPath;

    // Split path and query string
    size_t qPos = fullPath.find('?');
    if (qPos != std::string::npos) {
        req.path = fullPath.substr(0, qPos);
        std::string queryString = fullPath.substr(qPos + 1);

        // Parse query parameters
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

    // Parse headers
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;  // End of headers

        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos) {
            std::string key = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 1);
            // Trim leading space from value
            if (!value.empty() && value[0] == ' ') value.erase(0, 1);
            req.headers[key] = value;
        }
    }

    return req;
}

// ============================================================================
// Request Handler
// ============================================================================
void HttpServer::handleRequest(SOCKET client, const HttpRequest& req) {
    // Only handle GET requests
    if (req.method != "GET") {
        sendError(client, 405, "Method Not Allowed");
        return;
    }

    // Parse path: /stream/{fileId}
    // Path format: /stream/123?token=...&userId=...&ts=...
    const char* STREAM_PREFIX = "/stream/";
    if (req.path.compare(0, strlen(STREAM_PREFIX), STREAM_PREFIX) != 0) {
        // Health check endpoint
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

    // Extract fileId from path
    std::string fileIdStr = req.path.substr(strlen(STREAM_PREFIX));
    int64_t fileId = 0;
    try {
        fileId = std::stoll(fileIdStr);
    } catch (...) {
        sendError(client, 400, "Bad Request: invalid fileId");
        return;
    }

    // Validate token
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

    if (!validateToken(fileId, userId, ts, tokenIt->second)) {
        sendError(client, 403, "Forbidden: invalid or expired token");
        return;
    }

    // Get file size
    int64_t fileSize = m_storage->getFileSize(fileId);
    if (fileSize <= 0) {
        sendError(client, 404, "File Not Found or empty");
        return;
    }

    // Parse Range header
    int64_t rangeStart = 0;
    int64_t rangeEnd = fileSize - 1;

    auto rangeIt = req.headers.find("Range");
    if (rangeIt != req.headers.end()) {
        std::string rangeVal = rangeIt->second;
        // Format: "bytes=start-end" or "bytes=start-"
        if (rangeVal.compare(0, 6, "bytes=") == 0) {
            std::string rangeSpec = rangeVal.substr(6);
            size_t dashPos = rangeSpec.find('-');

            if (dashPos != std::string::npos) {
                std::string startStr = rangeSpec.substr(0, dashPos);
                std::string endStr = rangeSpec.substr(dashPos + 1);

                // Wrap stoll in try-catch: malformed Range headers
                // (e.g. "bytes=abc-def") would throw and crash the HTTP server thread
                try {
                    // F12-6 fix: suffix range — "bytes=-N" means last N bytes
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

    // Clamp range
    if (rangeStart < 0) rangeStart = 0;
    if (rangeEnd >= fileSize) rangeEnd = fileSize - 1;
    if (rangeStart > rangeEnd) {
        sendError(client, 416, "Range Not Satisfiable");
        return;
    }

    // Get file name from query params (for Content-Type)
    std::string fileName;
    auto nameIt = req.queryParams.find("name");
    if (nameIt != req.queryParams.end()) {
        fileName = nameIt->second;
    } else {
        fileName = "file.bin";
    }

    sendFile(client, fileId, rangeStart, rangeEnd, fileName);
}

// ============================================================================
// HTTP Response Helpers
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

void HttpServer::sendFile(SOCKET client, int64_t fileId,
                           int64_t rangeStart, int64_t rangeEnd,
                           const std::string& fileName) {
    int64_t fileSize = m_storage->getFileSize(fileId);
    int64_t contentLength = rangeEnd - rangeStart + 1;

    // Build response headers
    char header[4096];
    int headerLen;

    if (rangeStart == 0 && rangeEnd == fileSize - 1 && fileSize == contentLength) {
        // Full file — 200 OK (or send as 206 for consistency with Range support)
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
        // Partial content — 206
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

    // Send headers
    int sent = send(client, header, headerLen, 0);
    if (sent <= 0) return;

    // Stream file data in 64KB chunks
    const int CHUNK_SIZE = 65536;
    int64_t remaining = contentLength;
    int64_t currentOffset = rangeStart;

    while (remaining > 0) {
        int readLen = (int)((std::min)((int64_t)CHUNK_SIZE, remaining));

        std::string data = m_storage->readRange(fileId, currentOffset, readLen);
        if (data.empty()) break;  // Read error or EOF

        int chunkSent = send(client, data.c_str(), (int)data.size(), 0);
        if (chunkSent <= 0) break;  // Client disconnected

        remaining -= chunkSent;
        currentOffset += chunkSent;
    }
}

// ============================================================================
// Utility: Read CRLF-terminated line from socket
// ============================================================================
std::string HttpServer::readLine(SOCKET sock) {
    std::string line;
    char ch;
    while (line.size() < 8192) {
        int n = recv(sock, &ch, 1, 0);
        if (n <= 0) return "";
        if (ch == '\r') {
            // Peek for \n
            char next;
            int n2 = recv(sock, &next, 1, MSG_PEEK);
            if (n2 > 0 && next == '\n') {
                recv(sock, &next, 1, 0);  // consume \n
            }
            return line;
        }
        if (ch == '\n') return line;
        line += ch;
    }
    return line;
}
