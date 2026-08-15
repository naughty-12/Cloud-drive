#include "NodeManager.h"
#include "Packdef.h"
#include "ProtocolFactory.h"
#include "../config/ConfigParser.h"
#include <fstream>
#include <cstdio>
#include <chrono>
#include <sstream>
#include <cstring>

NodeManager::NodeManager() {}

NodeManager::~NodeManager() { shutdown(); }

bool NodeManager::init(const std::string& configPath) {
    // Read entire config file
    std::ifstream f(configPath);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    std::string json = ss.str();
    f.close();

    m_nodeId = jsonGetString(json, "node_id");
    m_listenIP = jsonGetString(json, "listen_ip");
    m_listenPort = jsonGetInt(json, "listen_port");
    m_storagePath = jsonGetString(json, "storage_path");

    if (m_nodeId.empty()) {
        printf("NodeManager: node_id not found in config\n");
        return false;
    }

    // Parse peers array
    size_t peersStart = json.find("\"peers\"");
    if (peersStart != std::string::npos) {
        size_t bracket = json.find('[', peersStart);
        size_t endBracket = json.find(']', bracket);
        if (bracket != std::string::npos && endBracket != std::string::npos) {
            std::string peersSection = json.substr(bracket + 1, endBracket - bracket - 1);
            // Parse each peer object
            size_t objStart = 0;
            while ((objStart = peersSection.find('{', objStart)) != std::string::npos) {
                size_t objEnd = peersSection.find('}', objStart);
                if (objEnd == std::string::npos) break;
                std::string peerObj = peersSection.substr(objStart, objEnd - objStart + 1);

                PeerNode node;
                node.nodeId = jsonGetString(peerObj, "id");
                node.ip = jsonGetString(peerObj, "ip");
                node.port = jsonGetInt(peerObj, "port");
                node.httpPort = jsonGetInt(peerObj, "http_port");
                if (node.httpPort == 0) node.httpPort = 8901; // default fallback
                node.sock = INVALID_SOCKET;
                node.connected = false;
                node.lastHeartbeat = 0;
                m_peers.push_back(node);

                objStart = objEnd + 1;
            }
        }
    }

    // Connect to all peers
    for (auto& peer : m_peers) {
        if (!connectToPeer(peer)) {
            printf("NodeManager: failed to connect to peer %s at %s:%d\n",
                peer.nodeId.c_str(), peer.ip.c_str(), peer.port);
        } else {
            printf("NodeManager: connected to peer %s at %s:%d\n",
                peer.nodeId.c_str(), peer.ip.c_str(), peer.port);
        }
    }

    // Start heartbeat thread
    m_running = true;
    m_heartbeatThread = std::thread(&NodeManager::heartbeatLoop, this);

    printf("NodeManager: initialized as %s on %s:%d with %zu peers\n",
        m_nodeId.c_str(), m_listenIP.c_str(), m_listenPort, m_peers.size());
    return true;
}

void NodeManager::shutdown() {
    m_running = false;
    if (m_heartbeatThread.joinable()) {
        m_heartbeatThread.join();
    }
    for (auto& peer : m_peers) {
        if (peer.sock != INVALID_SOCKET) {
            closesocket(peer.sock);
            peer.sock = INVALID_SOCKET;
        }
    }
}

uint64_t NodeManager::hashFileId(int64_t fileId) const {
    // FNV-1a 64-bit hash of fileId
    uint64_t hash = 14695981039346656037ULL;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(&fileId);
    for (size_t i = 0; i < sizeof(fileId); i++) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string NodeManager::getNodeForFile(int64_t fileId) const {
    std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
    std::vector<std::string> allNodes;
    allNodes.push_back(m_nodeId);
    for (const auto& p : m_peers) {
        allNodes.push_back(p.nodeId);
    }
    uint64_t h = hashFileId(fileId);
    return allNodes[h % allNodes.size()];
}

bool NodeManager::isLocal(int64_t fileId) const {
    return getNodeForFile(fileId) == m_nodeId;
}

std::string NodeManager::getRedirectIP(int64_t fileId) const {
    std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
    std::string targetNode = getNodeForFile(fileId);
    if (targetNode == m_nodeId) {
        return m_listenIP;
    }
    for (const auto& p : m_peers) {
        if (p.nodeId == targetNode) return p.ip;
    }
    return m_listenIP; // fallback
}

int NodeManager::getRedirectPort(int64_t fileId) const {
    std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
    std::string targetNode = getNodeForFile(fileId);
    if (targetNode == m_nodeId) {
        return m_listenPort;
    }
    for (const auto& p : m_peers) {
        if (p.nodeId == targetNode) return p.port;
    }
    return m_listenPort;
}

int NodeManager::getHttpPortForFile(int64_t fileId) const {
    std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
    std::string targetNode = getNodeForFile(fileId);
    if (targetNode == m_nodeId) {
        return 0;  // caller should use local m_httpPort
    }
    for (const auto& p : m_peers) {
        if (p.nodeId == targetNode) return p.httpPort;
    }
    return 0;  // not found — caller falls back to local
}

bool NodeManager::connectToPeer(PeerNode& peer) {
    peer.sock = socket(AF_INET, SOCK_STREAM, 0);
    if (peer.sock == INVALID_SOCKET) return false;

    // Set socket timeouts — prevents blocking IOCP/heartbeat threads
    // when peer is silently unreachable (e.g., firewall drops SYN without RST)
    int timeout = 3000;  // 3 seconds
    setsockopt(peer.sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(DWORD));
    setsockopt(peer.sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(DWORD));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(peer.ip.c_str());
    addr.sin_port = htons(peer.port);

    // Non-blocking connect + select — caps connect timeout at 3s
    // (SO_RCVTIMEO/SNDTIMEO only affect send/recv, not connect)
    u_long mode = 1;
    ioctlsocket(peer.sock, FIONBIO, &mode);
    int ret = connect(peer.sock, (sockaddr*)&addr, sizeof(addr));
    if (ret == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(peer.sock, &writeSet);
            struct timeval tv = {3, 0};  // 3 second connect timeout
            ret = select(0, NULL, &writeSet, NULL, &tv);
            if (ret <= 0) {
                // Timeout or error — peer unreachable
                closesocket(peer.sock);
                peer.sock = INVALID_SOCKET;
                return false;
            }
        } else {
            // Immediate failure (e.g., ECONNREFUSED)
            closesocket(peer.sock);
            peer.sock = INVALID_SOCKET;
            return false;
        }
    }
    // Restore blocking mode
    mode = 0;
    ioctlsocket(peer.sock, FIONBIO, &mode);

    peer.connected = true;
    peer.lastHeartbeat = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return true;
}

bool NodeManager::sendToPeer(PeerNode& peer, const char* data, int len) {
    if (!peer.connected || peer.sock == INVALID_SOCKET) {
        if (!connectToPeer(peer)) return false;
    }
    // 4-byte BE length prefix + data (same wire format as client)
    int32_t netLen = htonl(len);
    if (send(peer.sock, (char*)&netLen, 4, 0) <= 0) {
        peer.connected = false;
        return false;
    }
    if (send(peer.sock, data, len, 0) <= 0) {
        peer.connected = false;
        return false;
    }
    return true;
}

bool NodeManager::replicateBlock(int64_t fileId, int blockSeq, int64_t offset,
                                  const char* data, int len) {
    // Copy peer info under lock, then do blocking I/O outside lock
    struct PeerTarget { std::string id; SOCKET sock; bool connected; };
    std::vector<PeerTarget> targets;
    {
        std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
        std::string primaryNode = getNodeForFile(fileId);
        if (primaryNode == m_nodeId) {
            // This node is primary → replicate to ALL other peers (mirror copies)
            for (auto& peer : m_peers) {
                targets.push_back({peer.nodeId, peer.sock, peer.connected});
            }
        } else {
            // This node is NOT primary → replicate TO the primary node only
            for (auto& peer : m_peers) {
                if (peer.nodeId == primaryNode) {
                    targets.push_back({peer.nodeId, peer.sock, peer.connected});
                    break;
                }
            }
        }
    }

    STRU_REPLICATEBLOCKRQ rq;
    rq.m_fileId = fileId;
    rq.m_blockSeq = blockSeq;
    rq.m_offset = offset;
    rq.m_dataLen = len;
    memcpy(rq.m_szData, data, (len < MAXFILECONTENT ? len : MAXFILECONTENT));
    auto packet = ProtocolFactory::serializeReplicateBlockRQ(rq);

    bool allOk = true;
    for (auto& t : targets) {
        if (!t.connected || t.sock == INVALID_SOCKET) continue;
        int32_t netLen = htonl((int)packet.size());
        if (send(t.sock, (char*)&netLen, 4, 0) <= 0 ||
            send(t.sock, (const char*)packet.data(), (int)packet.size(), 0) <= 0) {
            printf("NodeManager: replicate failed to peer %s\n", t.id.c_str());
            allOk = false;
        }
    }
    return allOk;
}

void NodeManager::heartbeatLoop() {
    while (m_running) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (!m_running) break;

        // Copy disconnected peer info under lock, try reconnect outside lock
        struct ReconnectTarget { std::string id; std::string ip; int port; };
        std::vector<ReconnectTarget> toReconnect;
        {
            std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
            for (auto& peer : m_peers) {
                if (!peer.connected) {
                    toReconnect.push_back({peer.nodeId, peer.ip, peer.port});
                }
            }
        }
        for (auto& t : toReconnect) {
            // Use connectToPeer() for consistent timeout + non-blocking behavior
            std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
            for (auto& peer : m_peers) {
                if (peer.nodeId == t.id && !peer.connected) {
                    if (peer.sock != INVALID_SOCKET) closesocket(peer.sock);
                    peer.sock = INVALID_SOCKET;
                    connectToPeer(peer);
                    break;
                }
            }
        }
    }
}
