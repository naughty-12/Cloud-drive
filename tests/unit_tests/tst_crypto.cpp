/**
 * @file tst_crypto.cpp
 * @brief CryptoUtil (SHA-256) 单元测试 — NIST 向量 + 项目语义断言。
 *
 * 测试对象：shared/crypto/CryptoUtil.h/cpp（两端共用的单一来源实现，
 * 含流式 API 与 sparseFingerprintFromBlocks）。
 */

#include "tst_crypto.h"

#include <QtTest/QtTest>

#include "CryptoUtil.h"

#include <QFile>
#include <QTemporaryDir>

#include <string>

using std::string;

// ---------------------------------------------------------------------------
// NIST 标准测试向量 (FIPS 180-2)
// ---------------------------------------------------------------------------
void TestCrypto::sha256KnownVectors()
{
    // NIST 向量 1: 空串
    QCOMPARE(QString::fromStdString(CryptoUtil::sha256("")),
             QString("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

    // NIST 向量 2: "abc"
    QCOMPARE(QString::fromStdString(CryptoUtil::sha256("abc")),
             QString("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    // NIST 向量 3: 两区块消息 (64+ 字节, 覆盖 padding 走 else 分支)
    QCOMPARE(QString::fromStdString(CryptoUtil::sha256(
                 "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
             QString("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));

    // NIST 向量 4: 1,000,000 个 'a'
    QCOMPARE(QString::fromStdString(CryptoUtil::sha256(string(1000000, 'a'))),
             QString("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

// ---------------------------------------------------------------------------
// 二进制数据哈希 (char*, len 重载 + 含 NUL 数据)
// ---------------------------------------------------------------------------
void TestCrypto::sha256BinaryData()
{
    // 含 NUL 的二进制
    const char binary[] = { '\x00', '\x01', '\x02', '\xFF', '\x00', '\x10', '\x20' };
    const size_t len = sizeof(binary);

    // (data, len) 重载与 string 重载结果一致
    string viaLen = CryptoUtil::sha256(binary, len);
    string viaStr = CryptoUtil::sha256(string(binary, len));
    QCOMPARE(QString::fromStdString(viaLen), QString::fromStdString(viaStr));

    // 长度是 7 而不是 strlen=0 → 哈希不应等于空串哈希
    QVERIFY(viaLen != CryptoUtil::sha256(""));
    QCOMPARE(static_cast<int>(viaLen.size()), 64);   // 64 个 hex 字符
}

// ---------------------------------------------------------------------------
// 密码哈希性质
// ---------------------------------------------------------------------------
void TestCrypto::hashPasswordProperties()
{
    const string pw1 = "password123";
    const string pw2 = "password124";

    // 确定性: 同一密码两次哈希一致
    QCOMPARE(QString::fromStdString(CryptoUtil::hashPassword(pw1)),
             QString::fromStdString(CryptoUtil::hashPassword(pw1)));

    // 输出为 64 位 hex
    QCOMPARE(static_cast<int>(CryptoUtil::hashPassword(pw1).size()), 64);

    // 雪崩: 一位不同 → 哈希完全不同
    QVERIFY(CryptoUtil::hashPassword(pw1) != CryptoUtil::hashPassword(pw2));

    // 与 sha256(password + salt) 语义一致 (固定 salt 方案)
    QCOMPARE(QString::fromStdString(CryptoUtil::hashPassword(pw1)),
             QString::fromStdString(CryptoUtil::sha256(pw1 + "0323CloudDisk_SALT_2026")));
}

// ---------------------------------------------------------------------------
// 文件指纹 == 内容 SHA-256
// ---------------------------------------------------------------------------
void TestCrypto::fileFingerprintMatches()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const string content = "0323 Cloud Disk — file fingerprint test.\n";
    QString path = dir.path() + "/sample.txt";

    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content.c_str(), static_cast<qint64>(content.size()));
    f.close();

    QCOMPARE(QString::fromStdString(CryptoUtil::fileFingerprint(path.toStdString())),
             QString::fromStdString(CryptoUtil::sha256(content)));
}

// ---------------------------------------------------------------------------
// 文件不存在 → 空串 (调用方据此判定失败)
// ---------------------------------------------------------------------------
void TestCrypto::fileFingerprintMissingFile()
{
    QCOMPARE(QString::fromStdString(CryptoUtil::fileFingerprint("Z:/no/such/file.bin")),
             QString(""));
}

// ---------------------------------------------------------------------------
// 稀疏指纹: 小文件 (<4KB) — 文件版与块版一致
// 文件版语义: head = 整个文件, tail = 整个文件 (seekg(0) 后读 4096 截断到 EOF),
// 因此块版必须传 (content, content, size) 才能等价。
// ---------------------------------------------------------------------------
void TestCrypto::sparseFingerprintSmallFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const string content(100, 'x');   // 100 字节 < 4KB
    QString path = dir.path() + "/small.bin";

    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content.c_str(), static_cast<qint64>(content.size()));
    f.close();

    string fromFile = CryptoUtil::sparseFingerprint(path.toStdString());
    string fromBlocks = CryptoUtil::sparseFingerprintFromBlocks(content, content,
                                                                static_cast<uint64_t>(content.size()));
    QCOMPARE(QString::fromStdString(fromFile), QString::fromStdString(fromBlocks));
}

// ---------------------------------------------------------------------------
// 稀疏指纹: 大文件 (>8KB) — 文件版与块版一致
// 文件版语义: head = 前 4096 字节, tail = 后 4096 字节 (无重叠)。
// ---------------------------------------------------------------------------
void TestCrypto::sparseFingerprintLargeFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 10000 字节确定性内容
    string content;
    content.reserve(10000);
    for (int i = 0; i < 10000; ++i)
        content.push_back(static_cast<char>('a' + (i % 26)));

    QString path = dir.path() + "/large.bin";
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content.c_str(), static_cast<qint64>(content.size()));
    f.close();

    string head = content.substr(0, 4096);
    string tail = content.substr(content.size() - 4096);

    string fromFile = CryptoUtil::sparseFingerprint(path.toStdString());
    string fromBlocks = CryptoUtil::sparseFingerprintFromBlocks(head, tail,
                                                                static_cast<uint64_t>(content.size()));
    QCOMPARE(QString::fromStdString(fromFile), QString::fromStdString(fromBlocks));

    // 与全量指纹必然不同 (采样 vs 全量)
    QVERIFY(fromFile != CryptoUtil::fileFingerprint(path.toStdString()));
}

// ---------------------------------------------------------------------------
// 空文件 → 稀疏指纹回退为 sha256("")
// ---------------------------------------------------------------------------
void TestCrypto::sparseFingerprintEmptyFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString path = dir.path() + "/empty.bin";
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();

    QCOMPARE(QString::fromStdString(CryptoUtil::sparseFingerprint(path.toStdString())),
             QString("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

// ---------------------------------------------------------------------------
// 流式 API 与一次性 API 等价 (覆盖 64 字节块边界)
// ---------------------------------------------------------------------------
void TestCrypto::streamingMatchesOneShot()
{
    // 300 字节确定性数据
    string data;
    data.reserve(300);
    for (int i = 0; i < 300; ++i)
        data.push_back(static_cast<char>(i * 7 + 1));

    // 分块大小故意跨越 64 字节边界: 1, 2, 63, 64, 65, 105
    const size_t chunkSizes[] = { 1, 2, 63, 64, 65, 105 };
    CryptoUtil::Sha256Ctx ctx;
    CryptoUtil::sha256Init(&ctx);

    size_t offset = 0;
    for (size_t i = 0; i < sizeof(chunkSizes) / sizeof(chunkSizes[0]); ++i) {
        size_t n = chunkSizes[i];
        if (offset + n > data.size())
            n = data.size() - offset;
        CryptoUtil::sha256Update(&ctx,
                                 reinterpret_cast<const uint8_t*>(data.data() + offset), n);
        offset += n;
    }
    QCOMPARE(static_cast<int>(offset), 300);

    string streamed = CryptoUtil::sha256FinalHex(&ctx);
    string oneShot = CryptoUtil::sha256(data);
    QCOMPARE(QString::fromStdString(streamed), QString::fromStdString(oneShot));
}
