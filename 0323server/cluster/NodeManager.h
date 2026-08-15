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
    int         port;           // TCP port (e.g. 8898)
    int         httpPort;       // HTTP streaming port (e.g. 8901)
    SOCKET      sock;           // persistent TCP connection
    bool        connected;
    int64_t     lastHeartbeat;  // timestamp
};

class NodeManager {
public:
    using ReplicateCallback = std::function<void(const std::string& nodeId,
        int64_t fileId, int blockSeq, int64_t offset, const char* data, int len)>;

    NodeManager();
    ~NodeManager();

    // Load config and connect to peers
    bool init(const std::string& configPath);
    void shutdown();

    // Which node should own this file? (consistent hash)
    std::string getNodeForFile(int64_t fileId) const;

    // Is this file on the current node?
    bool isLocal(int64_t fileId) const;

    // Get the address a client should use for a file
    std::string getRedirectIP(int64_t fileId) const;
    int getRedirectPort(int64_t fileId) const;

    // Get the HTTP streaming port for the node that owns a file
    int getHttpPortForFile(int64_t fileId) const;

    // Send a replication block to the peer that should mirror this file
    bool replicateBlock(int64_t fileId, int blockSeq, int64_t offset, const char* data, int len);

    // Get all peer nodes
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
    mutable std::recursive_mutex m_peersMutex;  // recursive: getRedirect* → getNodeForFile re-locks
    std::atomic<bool> m_running{false};
    std::thread m_heartbeatThread;

    void heartbeatLoop();
    bool connectToPeer(PeerNode& peer);
    bool sendToPeer(PeerNode& peer, const char* data, int len);
    uint64_t hashFileId(int64_t fileId) const;
};

#endif
