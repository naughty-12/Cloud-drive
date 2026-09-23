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
    // 读取整个配置文件
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

    // 解析 peers 数组
    size_t peersStart = json.find("\"peers\"");
    if (peersStart != std::string::npos) {
        size_t bracket = json.find('[', peersStart);
        size_t endBracket = json.find(']', bracket);
        if (bracket != std::string::npos && endBracket != std::string::npos) {
            std::string peersSection = json.substr(bracket + 1, endBracket - bracket - 1);
            // 逐个解析对等节点对象
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
                if (node.httpPort == 0) node.httpPort = 8901; // 默认回退值
                node.sock = INVALID_SOCKET;
                node.connected = false;
                node.lastHeartbeat = 0;
                m_peers.push_back(node);

                objStart = objEnd + 1;
            }
        }
    }

    // 连接所有对等节点
    for (auto& peer : m_peers) {
        if (!connectToPeer(peer)) {
            printf("NodeManager: failed to connect to peer %s at %s:%d\n",
                peer.nodeId.c_str(), peer.ip.c_str(), peer.port);
        } else {
            printf("NodeManager: connected to peer %s at %s:%d\n",
                peer.nodeId.c_str(), peer.ip.c_str(), peer.port);
        }
    }

    // 启动心跳线程
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
    // 对 fileId 做 FNV-1a 64 位哈希
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
    return m_listenIP; // 回退
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
        return 0;  // 调用方应使用本机的 m_httpPort
    }
    for (const auto& p : m_peers) {
        if (p.nodeId == targetNode) return p.httpPort;
    }
    return 0;  // 未找到——调用方回退到本机
}

bool NodeManager::connectToPeer(PeerNode& peer) {
    peer.sock = socket(AF_INET, SOCK_STREAM, 0);
    if (peer.sock == INVALID_SOCKET) return false;

    // 设置 socket 超时——防止对等节点静默不可达时阻塞 IOCP/心跳线程
    // （例如防火墙丢弃 SYN 而不返回 RST）
    int timeout = 3000;  // 3 秒
    setsockopt(peer.sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(DWORD));
    setsockopt(peer.sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(DWORD));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(peer.ip.c_str());
    addr.sin_port = htons(peer.port);

    // 非阻塞 connect + select——将连接超时限制在 3 秒
    // （SO_RCVTIMEO/SNDTIMEO 只影响 send/recv，不影响 connect）
    u_long mode = 1;
    ioctlsocket(peer.sock, FIONBIO, &mode);
    int ret = connect(peer.sock, (sockaddr*)&addr, sizeof(addr));
    if (ret == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(peer.sock, &writeSet);
            struct timeval tv = {3, 0};  // 3 秒连接超时
            ret = select(0, NULL, &writeSet, NULL, &tv);
            if (ret <= 0) {
                // 超时或出错——对等节点不可达
                closesocket(peer.sock);
                peer.sock = INVALID_SOCKET;
                return false;
            }
        } else {
            // 立即失败（如 ECONNREFUSED）
            closesocket(peer.sock);
            peer.sock = INVALID_SOCKET;
            return false;
        }
    }
    // 恢复阻塞模式
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
    // 4 字节大端长度前缀 + 数据（与客户端相同的传输格式）
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
    // 在锁内拷贝对等节点信息，再在锁外执行阻塞 I/O
    struct PeerTarget { std::string id; SOCKET sock; bool connected; };
    std::vector<PeerTarget> targets;
    {
        std::lock_guard<std::recursive_mutex> lock(m_peersMutex);
        std::string primaryNode = getNodeForFile(fileId);
        if (primaryNode == m_nodeId) {
            // 本节点是主节点——复制到所有其他对等节点（镜像副本）
            for (auto& peer : m_peers) {
                targets.push_back({peer.nodeId, peer.sock, peer.connected});
            }
        } else {
            // 本节点不是主节点——仅复制到主节点
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

        // 在锁内拷贝断开的对等节点信息，再在锁外尝试重连
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
            // 使用 connectToPeer() 以保持一致的超时与非阻塞行为
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
