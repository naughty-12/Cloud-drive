/**
 * @file StreamAccessController.cpp
 * @brief 流媒体访问控制实现
 */

#include "StreamAccessController.h"
#include "CryptoUtil.h"

#include <ctime>
#include <sstream>

// ============================================================================
// 常量
// ============================================================================
// 保持与历史实现完全一致：改动这个值会让已签发的 URL 全部失效
static const char* kSecretSalt = "0323CloudDisk_HTTP_STREAM_2026";

const char* StreamAccessController::secretSalt()
{
    return kSecretSalt;
}

// ============================================================================
// 构造
// ============================================================================
StreamAccessController::StreamAccessController()
    : m_tokenTtlSeconds(60)        // 保持原有 60 秒签发窗口
    , m_sessionIdleSeconds(3600)   // 播放会话空闲 1 小时
    , m_maxSessions(4096)
{
}

// ============================================================================
// 签发（无状态）
// ============================================================================
std::string StreamAccessController::issueToken(int64_t userId, int64_t fileId,
                                               int64_t& outTs) const
{
    outTs = (int64_t)time(nullptr);

    // token = SHA-256(fileId | userId | timestamp | SECRET_SALT)
    std::ostringstream oss;
    oss << fileId << "|" << userId << "|" << outTs << "|" << kSecretSalt;
    return CryptoUtil::sha256(oss.str());
}

// ============================================================================
// 校验
// ============================================================================
StreamAccessController::Result StreamAccessController::check(
    int64_t userId, int64_t fileId, int64_t ts,
    const std::string& token, const std::string& peerIp, int64_t now)
{
    // 单边时间窗：拒绝未来时间戳（F12-3）
    int64_t age = now - ts;
    if (age < 0) return Invalid;

    // 重新计算期望的凭证，比对签名（不依赖会话状态）
    std::ostringstream oss;
    oss << fileId << "|" << userId << "|" << ts << "|" << kSecretSalt;
    if (token != CryptoUtil::sha256(oss.str())) return Invalid;

    std::lock_guard<std::mutex> lock(m_mutex);

    // --- 第 1 级：已有播放会话 ---
    // 播放器全程使用同一个 URL，所以"是否已激活"是拖进度条能否成功的关键：
    // 只要会话还在空闲窗口内，就刷新时间戳并放行（不再看 token 的 60 秒窗口）。
    auto it = m_sessions.find(token);
    if (it != m_sessions.end()) {
        Session& s = it->second;

        // 会话绑定来源 IP：别的机器拿同一个 token 重放 → 拒绝
        if (s.peerIp != peerIp) return Invalid;

        if (now - s.lastSeen <= m_sessionIdleSeconds) {
            s.lastSeen = now;   // 滑动窗口：只要还在看就一直续
            return Allow;
        }

        // 空闲超时：会话作废，退回第 2 级重新判定
        m_sessions.erase(it);
    }

    // --- 第 2 级：签发性凭证的短期窗口 ---
    if (age > m_tokenTtlSeconds) return Expired;

    // 首次通过 → 激活播放会话
    evictOldestIfFull();

    Session s;
    s.peerIp    = peerIp;
    s.createdAt = now;
    s.lastSeen  = now;
    m_sessions[token] = s;

    return Allow;
}

// ============================================================================
// 会话表维护
// ============================================================================
void StreamAccessController::evictOldestIfFull()
{
    // 线性扫描最久未使用的一条（会话数上限默认 4096，线性扫描成本可忽略）
    while (m_sessions.size() >= m_maxSessions && !m_sessions.empty()) {
        auto oldest = m_sessions.begin();
        for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
            if (it->second.lastSeen < oldest->second.lastSeen) oldest = it;
        }
        m_sessions.erase(oldest);
    }
}

size_t StreamAccessController::sessionCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sessions.size();
}

size_t StreamAccessController::pruneExpiredSessions(int64_t now)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    size_t removed = 0;
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ) {
        if (now - it->second.lastSeen > m_sessionIdleSeconds) {
            it = m_sessions.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}
