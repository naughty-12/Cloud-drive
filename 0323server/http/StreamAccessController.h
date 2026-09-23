#ifndef STREAMACCESSCONTROLLER_H
#define STREAMACCESSCONTROLLER_H

/**
 * @file StreamAccessController.h
 * @brief 流媒体访问控制 — 凭证签发 + 播放会话
 *
 * 背景（为什么要有这个类）：
 *   播放器拿到的是一个**带凭证的固定 URL**，它无法在播放中途更换凭证。
 *   而凭证是短期有效的（签发后 60 秒），播放器每次拖动进度条都会带同一个
 *   凭证发起新的 Range 请求 —— 于是"看了一分钟以后再拖进度条"必然 403。
 *
 * 解决思路（两级）：
 *   1. 签发性凭证（token）：仍然短期有效，用于"第一次获得播放授权"。
 *      这保留了"URL 被截获也很快失效"的安全属性（见 interview-qa 追问 5）。
 *   2. 播放会话（session）：首次校验通过后升级为会话，绑定**来源 IP**，
 *      在空闲窗口内允许同一 token 反复使用（拖进度条、续传、多连接）。
 *
 * 线程模型：
 *   - issueToken() 是无状态的，可由任意线程调用
 *     （实际由 tcpkernel 的 DB worker 线程在生成 StreamTokenRS 时调用）。
 *   - check() 会读写会话表，内部加锁，当前仅由 HttpServer 的 select 线程调用。
 *
 * 零外部依赖：只依赖 shared/crypto 的 CryptoUtil（SHA-256）。
 */

#include <cstdint>
#include <cstddef>
#include <map>
#include <mutex>
#include <string>

class StreamAccessController
{
public:
    /// 校验结果
    enum Result {
        Allow = 0,   ///< 放行
        Invalid,     ///< 凭证无效：签名不符 / 来源 IP 不符 / 时间戳在未来
        Expired      ///< 凭证与会话都已过期，需要重新签发
    };

    StreamAccessController();

    // --- 配置 ---

    /// 签发性凭证有效期（秒）。默认 60 —— 保持原有安全窗口不变
    void setTokenTtlSeconds(int seconds)      { m_tokenTtlSeconds = seconds; }
    int  tokenTtlSeconds() const              { return m_tokenTtlSeconds; }

    /// 播放会话空闲有效期（秒）。默认 3600 —— 最后一次请求后多久失效
    void setSessionIdleSeconds(int seconds)   { m_sessionIdleSeconds = seconds; }
    int  sessionIdleSeconds() const           { return m_sessionIdleSeconds; }

    /// 会话表上限（条）。默认 4096 —— 防止内存无限增长（0 视为 1）
    void setMaxSessions(size_t max)           { m_maxSessions = (max == 0) ? 1 : max; }

    // --- 核心接口 ---

    /**
     * 签发流媒体凭证（无状态，线程安全）
     * @param outTs 输出：凭证中使用的时间戳（要放进 URL 的 ts 参数）
     * @return token = SHA-256(fileId | userId | timestamp | SECRET_SALT)
     */
    std::string issueToken(int64_t userId, int64_t fileId, int64_t& outTs) const;

    /**
     * 校验一次流媒体请求（带会话状态）
     * @param peerIp 请求来源 IP —— 会话首次建立时记住，之后必须一致
     * @param now    当前时间戳（秒）。显式传入便于测试注入
     */
    Result check(int64_t userId, int64_t fileId, int64_t ts,
                 const std::string& token, const std::string& peerIp,
                 int64_t now);

    /// 当前活跃会话数（测试与日志用）
    size_t sessionCount() const;

    /// 清理空闲超时的会话，返回清理条数
    size_t pruneExpiredSessions(int64_t now);

    /// 凭证盐值（保持与历史实现一致，改动会让已签发的 URL 全部失效）
    static const char* secretSalt();

private:
    struct Session {
        std::string peerIp;     ///< 首次建立会话的来源 IP
        int64_t     createdAt;  ///< 会话建立时间
        int64_t     lastSeen;   ///< 最后一次使用时间（滑动窗口依据）
    };

    /// 会话表已满时淘汰最久未使用的条目
    void evictOldestIfFull();

    mutable std::mutex            m_mutex;
    std::map<std::string, Session> m_sessions;   ///< key = token

    int    m_tokenTtlSeconds;
    int    m_sessionIdleSeconds;
    size_t m_maxSessions;
};

#endif // STREAMACCESSCONTROLLER_H
