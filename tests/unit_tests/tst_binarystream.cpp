/**
 * @file tst_binarystream.cpp
 * @brief BinaryStream 二进制读写器单元测试。
 */

#include "tst_binarystream.h"

#include <QtTest/QtTest>

#include "BinaryStream.h"

#include <cstring>
#include <limits>
#include <string>
#include <vector>

using std::string;
using std::vector;

// ---------------------------------------------------------------------------
// int32 往返
// ---------------------------------------------------------------------------
void TestBinaryStream::int32RoundTrip()
{
    const int32_t values[] = {
        0, 1, -1, 42, 123456789, -987654321,
        std::numeric_limits<int32_t>::max(),
        std::numeric_limits<int32_t>::min(),
        0x01020304
    };

    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        BinaryStream bs;
        bs << values[i];
        QCOMPARE(bs.size(), 4);

        BinaryStream in = BinaryStream::fromData(
            reinterpret_cast<const char*>(bs.buffer().data()), bs.size());
        int32_t back = 0;
        in >> back;
        QCOMPARE(back, values[i]);
        QCOMPARE(in.remaining(), 0);
    }
}

// ---------------------------------------------------------------------------
// int64 往返
// ---------------------------------------------------------------------------
void TestBinaryStream::int64RoundTrip()
{
    const int64_t values[] = {
        0, 1, -1, 4096,
        0x0102030405060708LL,
        std::numeric_limits<int64_t>::max(),
        std::numeric_limits<int64_t>::min(),
        13800138000LL   // 手机号量级
    };

    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        BinaryStream bs;
        bs << values[i];
        QCOMPARE(bs.size(), 8);

        BinaryStream in = BinaryStream::fromData(
            reinterpret_cast<const char*>(bs.buffer().data()), bs.size());
        int64_t back = 0;
        in >> back;
        QCOMPARE(back, values[i]);
    }
}

// ---------------------------------------------------------------------------
// 变长字符串往返 (2 字节长度前缀)
// ---------------------------------------------------------------------------
void TestBinaryStream::stringRoundTrip()
{
    const string samples[] = {
        string(""),                        // 空串
        string("hello"),                   // 短串
        string("你好，世界"),               // UTF-8 中文
        string(300, 'x'),                  // 长串 (300 < 65535 上限)
        string("SHA-256: ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
    };

    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        BinaryStream bs;
        bs << samples[i];
        // 长度前缀 2 字节 + 内容
        QCOMPARE(bs.size(), 2 + static_cast<int>(samples[i].size()));

        BinaryStream in = BinaryStream::fromData(
            reinterpret_cast<const char*>(bs.buffer().data()), bs.size());
        string back;
        in >> back;
        QCOMPARE(back, samples[i]);
        QCOMPARE(in.remaining(), 0);
    }
}

// ---------------------------------------------------------------------------
// 定长字符串往返 (补零 / 截断 / 恰满)
// ---------------------------------------------------------------------------
void TestBinaryStream::fixedStringRoundTrip()
{
    // 1) 短串 → 零填充到 65 字节
    {
        BinaryStream bs;
        bs.writeFixedString("abc", 65);
        QCOMPARE(bs.size(), 65);
        // 尾部应为 0
        const vector<uint8_t>& buf = bs.buffer();
        for (int i = 3; i < 65; ++i)
            QCOMPARE(static_cast<int>(buf[i]), 0);

        BinaryStream in = BinaryStream::fromData(
            reinterpret_cast<const char*>(buf.data()), bs.size());
        char out[65] = {0};
        in.readFixedString(out, 65);
        QCOMPARE(string(out), string("abc"));
    }

    // 2) 恰满 65 字节 → 前 64 字节保留, 最后 1 字节被强制置 '\0'
    //    (readFixedString 设计语义: 保证 null 终止, 与 C 字符串字段一致)
    {
        string exact(65, 'z');
        BinaryStream bs;
        bs.writeFixedString(exact.c_str(), 65);
        BinaryStream in = BinaryStream::fromData(
            reinterpret_cast<const char*>(bs.buffer().data()), bs.size());
        char out[65] = {0};
        in.readFixedString(out, 65);
        QVERIFY(memcmp(out, exact.c_str(), 64) == 0);   // 前 64 字节原样
        QCOMPARE(out[64], '\0');                        // 末字节强制终止
    }

    // 3) 超长 → 截断到 fixedSize (同样受 null 终止语义约束)
    {
        string longStr(100, 'q');
        BinaryStream bs;
        bs.writeFixedString(longStr.c_str(), 65);
        QCOMPARE(bs.size(), 65);
        BinaryStream in = BinaryStream::fromData(
            reinterpret_cast<const char*>(bs.buffer().data()), bs.size());
        char out[65] = {0};
        in.readFixedString(out, 65);
        QVERIFY(memcmp(out, longStr.c_str(), 64) == 0);   // 前 64 字节为截断内容
        QCOMPARE(out[64], '\0');                          // 末字节强制终止
    }
}

// ---------------------------------------------------------------------------
// 原始字节往返 (含 NUL 的二进制数据)
// ---------------------------------------------------------------------------
void TestBinaryStream::rawRoundTrip()
{
    const char raw[] = { '\x00', '\x01', '\x02', '\xFF', '\x00', 'A', 'B', '\x80' };
    const int rawLen = static_cast<int>(sizeof(raw));

    BinaryStream bs;
    bs.writeRaw(raw, rawLen);
    QCOMPARE(bs.size(), rawLen);

    BinaryStream in = BinaryStream::fromData(
        reinterpret_cast<const char*>(bs.buffer().data()), bs.size());
    char out[8] = {0};
    in.readRaw(out, rawLen);
    QVERIFY(memcmp(out, raw, rawLen) == 0);
    QCOMPARE(in.remaining(), 0);
}

// ---------------------------------------------------------------------------
// 混合字段单流往返 (模拟真实协议包的字段组合)
// ---------------------------------------------------------------------------
void TestBinaryStream::mixedFieldRoundTrip()
{
    BinaryStream bs;
    bs << static_cast<int32_t>(12345);
    bs << string("report.txt");
    bs << static_cast<int64_t>(0x1122334455667788LL);
    bs.writeFixedString("2026-08-20 12:00:00", 65);
    bs.writeRaw("PAYLOAD", 7);

    BinaryStream in = BinaryStream::fromData(
        reinterpret_cast<const char*>(bs.buffer().data()), bs.size());

    int32_t i = 0;
    string s;
    int64_t l = 0;
    char fixed[65] = {0};
    char raw[8] = {0};

    in >> i >> s >> l;
    in.readFixedString(fixed, 65);
    in.readRaw(raw, 7);
    raw[7] = '\0';

    QCOMPARE(i, 12345);
    QCOMPARE(s, string("report.txt"));
    QCOMPARE(l, static_cast<int64_t>(0x1122334455667788LL));
    QCOMPARE(string(fixed), string("2026-08-20 12:00:00"));
    QCOMPARE(string(raw), string("PAYLOAD"));
    QCOMPARE(in.remaining(), 0);
}

// ---------------------------------------------------------------------------
// 网络字节序 (大端) 字节级验证
// ---------------------------------------------------------------------------
void TestBinaryStream::bigEndianWireFormat()
{
    // int32: 0x01020304 → 字节序 [01 02 03 04]
    {
        BinaryStream bs;
        bs << static_cast<int32_t>(0x01020304);
        const vector<uint8_t> expect = { 0x01, 0x02, 0x03, 0x04 };
        QVERIFY(bs.buffer() == expect);
    }

    // int64: 0x0102030405060708 → [01 02 03 04 05 06 07 08]
    {
        BinaryStream bs;
        bs << static_cast<int64_t>(0x0102030405060708LL);
        const vector<uint8_t> expect = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
        QVERIFY(bs.buffer() == expect);
    }

    // 字符串长度前缀: "ab" → [00 02 61 62]
    {
        BinaryStream bs;
        bs << string("ab");
        const vector<uint8_t> expect = { 0x00, 0x02, 0x61, 0x62 };
        QVERIFY(bs.buffer() == expect);
    }
}

// ---------------------------------------------------------------------------
// fromData 构造读取流
// ---------------------------------------------------------------------------
void TestBinaryStream::fromDataReads()
{
    // 手工构造 [00 00 00 2A] 大端 int32 = 42
    const char bytes[4] = { '\x00', '\x00', '\x00', '\x2A' };
    BinaryStream in = BinaryStream::fromData(bytes, 4);
    int32_t val = 0;
    in >> val;
    QCOMPARE(val, 42);
    QCOMPARE(in.remaining(), 0);

    // null / 空输入 → 空流, 读取抛异常
    BinaryStream empty = BinaryStream::fromData(nullptr, 0);
    QCOMPARE(empty.size(), 0);
}

// ---------------------------------------------------------------------------
// 越界读取必须抛 std::runtime_error
// ---------------------------------------------------------------------------
void TestBinaryStream::overflowThrows()
{
    // 空流读 int32
    {
        BinaryStream bs;
        int32_t v = 0;
        QVERIFY_EXCEPTION_THROWN(bs >> v, std::runtime_error);
    }

    // 空流读 int64
    {
        BinaryStream bs;
        int64_t v = 0;
        QVERIFY_EXCEPTION_THROWN(bs >> v, std::runtime_error);
    }

    // 长度前缀声称 100 字节, 实际只有 3 字节 → 越界
    {
        BinaryStream bs;
        bs << string("abc");   // 5 字节: [00 03] + abc
        BinaryStream in = BinaryStream::fromData(
            reinterpret_cast<const char*>(bs.buffer().data()), bs.size());
        string v;
        // 手工构造: 先读掉前缀+内容(合法), 然后试图再读一个超长串
        in >> v;
        QVERIFY_EXCEPTION_THROWN(in >> v, std::runtime_error);
    }

    // 定长读取超出剩余字节
    {
        BinaryStream bs;
        bs << static_cast<int32_t>(1);   // 仅 4 字节
        BinaryStream in = BinaryStream::fromData(
            reinterpret_cast<const char*>(bs.buffer().data()), bs.size());
        char buf[65] = {0};
        QVERIFY_EXCEPTION_THROWN(in.readFixedString(buf, 65), std::runtime_error);
    }

    // readRaw 超出剩余字节
    {
        BinaryStream bs;
        bs << static_cast<int32_t>(1);
        BinaryStream in = BinaryStream::fromData(
            reinterpret_cast<const char*>(bs.buffer().data()), bs.size());
        char buf[8] = {0};
        QVERIFY_EXCEPTION_THROWN(in.readRaw(buf, 8), std::runtime_error);
    }
}

// ---------------------------------------------------------------------------
// reset / remaining / readPos 语义
// ---------------------------------------------------------------------------
void TestBinaryStream::resetAndRemaining()
{
    BinaryStream bs;
    bs << static_cast<int32_t>(1) << string("abc");
    QCOMPARE(bs.size(), 4 + 5);

    BinaryStream in = BinaryStream::fromData(
        reinterpret_cast<const char*>(bs.buffer().data()), bs.size());
    QCOMPARE(in.remaining(), bs.size());
    QCOMPARE(in.readPos(), 0);

    int32_t v = 0;
    in >> v;
    QCOMPARE(in.readPos(), 4);
    QCOMPARE(in.remaining(), 5);

    in.reset();
    QCOMPARE(in.size(), 0);
    QCOMPARE(in.remaining(), 0);
    QCOMPARE(in.readPos(), 0);
}
