#ifndef HTTPSERVER_H
#define HTTPSERVER_H

// FD_SETSIZE must be defined before winsock2.h to raise the default 64-socket limit
#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif

// WinSock2 must be included before windows.h (which Qt may pull in)
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <map>
#include <atomic>
#include <thread>
#include <cstdint>

// Forward declaration
class FileStorage;

// ============================================================================
// HttpServer — Lightweight HTTP/1.1 streaming server
// ============================================================================
// Runs on a separate port (default 8900), independent of the IOCP TCP server.
// Supports:
//   - GET /stream/{fileId}?token=...&userId=...&ts=...  (file streaming)
//   - Range header parsing → 206 Partial Content
//   - Token-based access control (SHA-256 + timestamp, 60s expiry)
//   - select()-based I/O multiplexing (no per-connection threads)
//
// Design decisions:
//   - Zero external dependencies (pure WinSock2 + C++11)
//   - Uses select() instead of IOCP for simplicity (low connection count)
//   - Token = SHA-256(fileId + userId + timestamp + SECRET_SALT)
// ============================================================================

class HttpServer {
public:
    HttpServer();
    ~HttpServer();

    // Start the HTTP server on the given port (background thread)
    bool start(int port, FileStorage* storage);

    // Stop the server, close all connections
    void stop();

    // Check if the server is running
    bool isRunning() const { return m_running; }

    // Get the listening port
    int port() const { return m_port; }

    // Generate a streaming token for the given user+file
    // Token = SHA-256(fileId + userId + timestamp + SECRET_SALT)
    // @param outTs  Output: the Unix timestamp used in the token (for URL)
    std::string generateToken(int64_t userId, int64_t fileId, int64_t& outTs);

private:
    // Main server loop (runs in background thread)
    void run();

    // HTTP request structure
    struct HttpRequest {
        std::string method;
        std::string path;
        std::map<std::string, std::string> headers;
        std::map<std::string, std::string> queryParams;
    };

    // Parse raw HTTP request text
    HttpRequest parseRequest(const std::string& raw);

    // Handle a single HTTP request
    void handleRequest(SOCKET client, const HttpRequest& req);

    // Send an error response
    void sendError(SOCKET client, int code, const std::string& message);

    // Stream file data with Range support
    void sendFile(SOCKET client, int64_t fileId, int64_t rangeStart, int64_t rangeEnd,
                  const std::string& fileName);

    // Validate streaming token
    bool validateToken(int64_t fileId, int64_t userId, int64_t ts, const std::string& token);

    // Get MIME type from file extension
    static std::string getMimeType(const std::string& fileName);

    // Read a line from a socket (CRLF-terminated)
    static std::string readLine(SOCKET sock);

    FileStorage* m_storage;
    SOCKET m_listenSocket;
    int m_port;
    std::atomic<bool> m_running;
    std::thread m_thread;

    // Token security
    static const char* SECRET_SALT;
    static const int TOKEN_TIMEOUT_SECONDS = 60;
};

#endif // HTTPSERVER_H
