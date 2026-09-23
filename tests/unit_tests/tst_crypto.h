#ifndef TST_CRYPTO_H
#define TST_CRYPTO_H

#include <QObject>

/**
 * CryptoUtil (SHA-256) 测试。
 * 覆盖: NIST 标准测试向量、密码哈希性质、文件指纹、
 * 稀疏指纹 (文件版 vs 块版一致性)、流式 API 与一次性 API 等价性。
 */
class TestCrypto : public QObject
{
    Q_OBJECT
private slots:
    void sha256KnownVectors();          // NIST 标准测试向量
    void sha256BinaryData();            // 二进制数据 (含 NUL) 哈希
    void hashPasswordProperties();      // 密码哈希: 确定性/长度/雪崩
    void fileFingerprintMatches();      // 文件指纹 == 内容 SHA-256
    void fileFingerprintMissingFile();  // 文件不存在 → 空串
    void sparseFingerprintSmallFile();  // 小文件 (<4KB): 文件版 vs 块版
    void sparseFingerprintLargeFile();  // 大文件 (>8KB): 文件版 vs 块版
    void sparseFingerprintEmptyFile();  // 空文件 → sha256("")
    void streamingMatchesOneShot();     // 流式分块哈希 == 一次性哈希
};

#endif // TST_CRYPTO_H
