/**
 * @file tst_streamaccess.cpp
 * @brief StreamAccessController 单元测试实现
 */

#include "tst_streamaccess.h"
#include "StreamAccessController.h"

#include <string>

using std::string;

// ============================================================================
// 测试辅助
// ============================================================================
TestStreamAccess::Issued TestStreamAccess::issue(StreamAccessController& ctl,
                                                 int64_t userId, int64_t fileId)
{
    Issued out;
    out.token = ctl.issueToken(userId, fileId, out.ts);
    return out;
}

/// 断言辅助：把枚举转成 int 再比，避免 Qt Test 对自定义类型的 toString 限制
static int asInt(StreamAccessController::Result r) { return static_cast<int>(r); }

// ============================================================================
// 签发性凭证 —— 原有语义必须保持不变
// ============================================================================

/// 刚签发的 token 立即可用，并激活一个播放会话
void TestStreamAccess::freshTokenIsAllowed()
{
    StreamAccessController ctl;
    Issued i = issue(ctl, 42, 103);

    QVERIFY(!i.token.empty());
    QVERIFY(i.ts > 0);

    QCOMPARE(asInt(ctl.check(42, 103, i.ts, i.token, "127.0.0.1", i.ts)),
             asInt(StreamAccessController::Allow));
    QCOMPARE(ctl.sessionCount(), size_t(1));
}

/// 未使用的 token 过了 TTL 直接失效，并且不会留下会话
void TestStreamAccess::unusedTokenExpiresAfterTtl()
{
    StreamAccessController ctl;
    ctl.setTokenTtlSeconds(60);
    Issued i = issue(ctl, 42, 103);

    QCOMPARE(asInt(ctl.check(42, 103, i.ts, i.token, "127.0.0.1", i.ts + 61)),
             asInt(StreamAccessController::Expired));
    QCOMPARE(ctl.sessionCount(), size_t(0));
}

/// 伪造的 token 被拒绝
void TestStreamAccess::forgedTokenIsRejected()
{
    StreamAccessController ctl;
    Issued i = issue(ctl, 42, 103);

    string forged = i.token;
    forged[0] = (forged[0] == 'a') ? 'b' : 'a';

    QCOMPARE(asInt(ctl.check(42, 103, i.ts, forged, "127.0.0.1", i.ts)),
             asInt(StreamAccessController::Invalid));
    QCOMPARE(ctl.sessionCount(), size_t(0));
}

/// 签名绑定 fileId + userId：同一个 token 不能用于别的文件
void TestStreamAccess::tokenBoundToFileAndUser()
{
    StreamAccessController ctl;
    Issued i = issue(ctl, 42, 103);

    QCOMPARE(asInt(ctl.check(42, 104, i.ts, i.token, "127.0.0.1", i.ts)),
             asInt(StreamAccessController::Invalid));
    QCOMPARE(asInt(ctl.check(43, 103, i.ts, i.token, "127.0.0.1", i.ts)),
             asInt(StreamAccessController::Invalid));
}

/// 拒绝未来时间戳（单边时间窗，防伪造时间）
void TestStreamAccess::futureTimestampIsRejected()
{
    StreamAccessController ctl;
    Issued i = issue(ctl, 42, 103);

    QCOMPARE(asInt(ctl.check(42, 103, i.ts, i.token, "127.0.0.1", i.ts - 10)),
             asInt(StreamAccessController::Invalid));
}

// ============================================================================
// 播放会话 —— 修复"播放超过 60 秒后拖动进度条 403"
// ============================================================================

/**
 * ★ 核心回归测试：播放器在整个播放期间反复发 Range 请求，
 *   但 URL 里的 token 只签发了 60 秒 —— 会话必须让后续请求继续通过。
 */
void TestStreamAccess::sessionSurvivesBeyondTokenTtl()
{
    StreamAccessController ctl;
    ctl.setTokenTtlSeconds(60);
    ctl.setSessionIdleSeconds(3600);

    Issued i = issue(ctl, 42, 103);

    // t=0：播放器首次拉流 → 允许，并激活会话
    QCOMPARE(asInt(ctl.check(42, 103, i.ts, i.token, "127.0.0.1", i.ts)),
             asInt(StreamAccessController::Allow));

    // t=10 分钟：用户拖动进度条 → 新请求，token 早已超过 60 秒，仍必须允许
    QCOMPARE(asInt(ctl.check(42, 103, i.ts, i.token, "127.0.0.1", i.ts + 600)),
             asInt(StreamAccessController::Allow));

    // t=59 分钟：继续播放仍然允许
    QCOMPARE(asInt(ctl.check(42, 103, i.ts, i.token, "127.0.0.1", i.ts + 3540)),
             asInt(StreamAccessController::Allow));

    // 全程只应该有一个会话
    QCOMPARE(ctl.sessionCount(), size_t(1));
}

/// 会话空闲超过窗口后失效（下次请求需要重新签发）
void TestStreamAccess::sessionExpiresAfterIdle()
{
    StreamAccessController ctl;
    ctl.setTokenTtlSeconds(60);
    ctl.setSessionIdleSeconds(600);

    Issued i = issue(ctl, 42, 103);

    QCOMPARE(asInt(ctl.check(42, 103, i.ts, i.token, "127.0.0.1", i.ts)),
             asInt(StreamAccessController::Allow));

    // 空闲 601 秒 → 会话失效，token 又早已过期 → 拒绝
    QCOMPARE(asInt(ctl.check(42, 103, i.ts, i.token, "127.0.0.1", i.ts + 601)),
             asInt(StreamAccessController::Expired));
    QCOMPARE(ctl.sessionCount(), size_t(0));
}

/// 会话绑定来源 IP：别的机器拿同一个 token 不能继续用（防局域网抓包重放）
void TestStreamAccess::sessionRejectsDifferentPeerIp()
{
    StreamAccessController ctl;
    ctl.setTokenTtlSeconds(60);
    ctl.setSessionIdleSeconds(3600);

    Issued i = issue(ctl, 42, 103);

    // 合法来源建立会话
    QCOMPARE(asInt(ctl.check(42, 103, i.ts, i.token, "127.0.0.1", i.ts)),
             asInt(StreamAccessController::Allow));

    // 抓包者从另一个 IP 重放 → 拒绝
    QCOMPARE(asInt(ctl.check(42, 103, i.ts, i.token, "10.0.0.99", i.ts + 5)),
             asInt(StreamAccessController::Invalid));

    // 合法来源不受影响
    QCOMPARE(asInt(ctl.check(42, 103, i.ts, i.token, "127.0.0.1", i.ts + 10)),
             asInt(StreamAccessController::Allow));
}

/// 会话表有上限，不会无限增长
void TestStreamAccess::sessionCountIsCapped()
{
    StreamAccessController ctl;
    ctl.setTokenTtlSeconds(600);
    ctl.setMaxSessions(2);

    for (int n = 0; n < 5; n++) {
        Issued i = issue(ctl, 42, 100 + n);
        QCOMPARE(asInt(ctl.check(42, 100 + n, i.ts, i.token, "127.0.0.1", i.ts + n)),
                 asInt(StreamAccessController::Allow));
    }

    QVERIFY(ctl.sessionCount() <= size_t(2));
}
