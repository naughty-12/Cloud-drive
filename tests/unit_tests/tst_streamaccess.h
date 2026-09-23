#ifndef TST_STREAMACCESS_H
#define TST_STREAMACCESS_H

/**
 * @file tst_streamaccess.h
 * @brief StreamAccessController 单元测试 — 流媒体凭证签发 + 播放会话
 *
 * 覆盖:
 *   - 签发性凭证本身仍是短期有效（保留 60 秒窗口 → 明文 HTTP 的安全缓解不推翻）
 *   - 新增播放会话：首次校验通过后升级，会话内多次 Range 请求放行（修 seek 403）
 *   - 会话绑定来源 IP（防局域网抓包重放）
 *   - 会话空闲超时、会话数上限、伪造 token、未来时间戳
 */

#include <QtTest/QtTest>

class TestStreamAccess : public QObject
{
    Q_OBJECT

private slots:
    // --- 签发性凭证（保留原有语义） ---
    void freshTokenIsAllowed();
    void unusedTokenExpiresAfterTtl();      // 未激活的 token 过了 TTL 就失效
    void forgedTokenIsRejected();
    void tokenBoundToFileAndUser();
    void futureTimestampIsRejected();

    // --- 播放会话（修复 seek 403） ---
    void sessionSurvivesBeyondTokenTtl();   // ★ 核心：60 秒后拖进度条不再被拒
    void sessionExpiresAfterIdle();
    void sessionRejectsDifferentPeerIp();
    void sessionCountIsCapped();

private:
    /// 测试辅助：签发一个 token，返回 (token, ts)
    struct Issued { std::string token; int64_t ts; };
    Issued issue(class StreamAccessController& ctl, int64_t userId, int64_t fileId);
};

#endif // TST_STREAMACCESS_H
