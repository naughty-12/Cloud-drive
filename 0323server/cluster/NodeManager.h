#ifndef NODEMANAGER_H
#define NODEMANAGER_H

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include <thread>
#include <atomic>
#include <winsock2.h>
#include <functional>

struct PeerNode {
    std::string nodeId;
    std::string ip;
    int         port;           // TCP 端口（如 8898）
    int         httpPort;       // HTTP 流媒体端口（如 8901）
    SOCKET      sock;           // 持久的 TCP 连接
    bool        connected;
    int64_t     lastHeartbeat;  // 时间戳
};

class NodeManager {
public:
    using ReplicateCallback = std::function<void(const std::string& nodeId,
        int64_t fileId, int blockSeq, int64_t offset, const char* data, int len)>;

    NodeManager();
    ~NodeManager();

    // 加载配置并连接对等节点
    bool init(const std::string& configPath);
    void shutdown();

    // 该文件应由哪个节点持有？（FNV-1a 哈希后按节点数取模的简化分片，无虚拟节点）
    std::string getNodeForFile(int64_t fileId) const;

    // 该文件是否位于当前节点？
    bool isLocal(int64_t fileId) const;

    // 获取客户端访问某文件时应使用的地址
    std::string getRedirectIP(int64_t fileId) const;
    int getRedirectPort(int64_t fileId) const;

    // 获取持有某文件的节点的 HTTP 流媒体端口
    int getHttpPortForFile(int64_t fileId) const;

    // 向应镜像该文件的对等节点发送复制块
    bool replicateBlock(int64_t fileId, int blockSeq, int64_t offset, const char* data, int len);

    // 获取全部对等节点
    const std::vector<PeerNode>& getPeers() const { return m_peers; }

    std::string nodeId() const { return m_nodeId; }
    int listenPort() const { return m_listenPort; }
    std::string listenIP() const { return m_listenIP; }

private:
    std::string m_nodeId;
    std::string m_listenIP;
    int         m_listenPort;
    std::string m_storagePath;

    std::vector<PeerNode> m_peers;
    mutable std::recursive_mutex m_peersMutex;  // 递归锁：getRedirect* → getNodeForFile 会再次加锁
    std::atomic<bool> m_running{false};
    std::thread m_heartbeatThread;

    void heartbeatLoop();
    bool connectToPeer(PeerNode& peer);
    bool sendToPeer(PeerNode& peer, const char* data, int len);
    uint64_t hashFileId(int64_t fileId) const;
};

#endif
