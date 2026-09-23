/**
 * @file tst_txtextractor.cpp
 * @brief TxtTextExtractor 单元测试 — 纯文本族解析器 (ITextExtractor 的第一个实现)。
 *
 * 测试对象：0323server/ai/local/ITextExtractor.h、TxtTextExtractor.h/cpp
 */

#include "tst_txtextractor.h"

#include <QtTest/QtTest>

#include "ITextExtractor.h"
#include "TxtTextExtractor.h"

#include <string>

using std::string;

// ---------------------------------------------------------------------------
// 扩展名判定: 纯文本族可处理, 二进制族不可 (→ 上层走文件名/扩展名兜底)
// ---------------------------------------------------------------------------
void TestTxtExtractor::handlesTextFiles()
{
    TxtTextExtractor ex;
    QVERIFY(ex.canHandle("readme.md"));
    QVERIFY(ex.canHandle("a.cpp"));
    QVERIFY(ex.canHandle("sql/init-db.sql"));
    QVERIFY(ex.canHandle("server.conf"));
    QVERIFY(ex.canHandle("notes.txt"));
    QVERIFY(!ex.canHandle("photo.jpg"));
    QVERIFY(!ex.canHandle("report.pdf"));
    QVERIFY(!ex.canHandle("archive.zip"));
    QVERIFY(!ex.canHandle("noext"));
}

// ---------------------------------------------------------------------------
// 大小写不敏感 (Windows 文件名常见 .TXT/.Cpp)
// ---------------------------------------------------------------------------
void TestTxtExtractor::handlesCaseInsensitive()
{
    TxtTextExtractor ex;
    QVERIFY(ex.canHandle("README.MD"));
    QVERIFY(ex.canHandle("Main.CPP"));
}

// ---------------------------------------------------------------------------
// UTF-8 BOM 剥离 (Windows 记事本存的 txt 常带 BOM)
// ---------------------------------------------------------------------------
void TestTxtExtractor::extractStripsBom()
{
    TxtTextExtractor ex;
    const string bom = "\xEF\xBB\xBFhello";
    QCOMPARE(ex.extract(bom), string("hello"));
    QCOMPARE(ex.extract(bom).size(), static_cast<size_t>(5));
}

// ---------------------------------------------------------------------------
// 普通文本原样返回 (含换行/中文, 不截断)
// ---------------------------------------------------------------------------
void TestTxtExtractor::extractKeepsPlainText()
{
    TxtTextExtractor ex;
    const string plain = "mysql 数据库连接池\n第二行";
    QCOMPARE(ex.extract(plain), plain);

    // 仅 2 字节的伪 BOM 前缀不应被误剥
    const string shortBom = "\xEF\xBB";
    QCOMPARE(ex.extract(shortBom), shortBom);
}

// ---------------------------------------------------------------------------
// 资源上限: 超长内容截断 (只取前缀做统计, 控内存/CPU)
// ---------------------------------------------------------------------------
void TestTxtExtractor::extractTruncatesHuge()
{
    TxtTextExtractor ex;
    const string big(200000, 'a');
    const string out = ex.extract(big);
    QVERIFY(out.size() <= TxtTextExtractor::kMaxExtract);
    QCOMPARE(out.size(), static_cast<size_t>(64 * 1024));

    // 恰好等于上限 / 空串 边界
    const string exact(64 * 1024, 'b');
    QCOMPARE(ex.extract(exact).size(), static_cast<size_t>(64 * 1024));
    QCOMPARE(ex.extract(string()).size(), static_cast<size_t>(0));
}

// ---------------------------------------------------------------------------
// 经接口指针多态调用 (搜索/预览/标签只依赖 ITextExtractor)
// ---------------------------------------------------------------------------
void TestTxtExtractor::usableThroughInterface()
{
    TxtTextExtractor impl;
    const ITextExtractor* ex = &impl;
    QVERIFY(ex->canHandle("x.txt"));
    QCOMPARE(ex->extract("plain"), string("plain"));

    // 通过基类指针 delete 必须安全 (虚析构)
    ITextExtractor* owned = new TxtTextExtractor();
    delete owned;
}
