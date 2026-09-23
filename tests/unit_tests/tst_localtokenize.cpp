/**
 * @file tst_localtokenize.cpp
 * @brief LocalTokenize 单元测试 — 词元化 (ASCII 整词 + UTF-8 中文 2-gram + 停用词)。
 *
 * 测试对象：0323server/ai/local/LocalTokenize.h/cpp
 */

#include "tst_localtokenize.h"

#include <QtTest/QtTest>

#include "LocalTokenize.h"

#include <algorithm>
#include <string>
#include <vector>

using std::string;
using std::vector;

namespace {

bool has(const vector<string>& tokens, const string& term)
{
    return std::find(tokens.begin(), tokens.end(), term) != tokens.end();
}

} // namespace

// ---------------------------------------------------------------------------
// ASCII 词: 连续字母/数字/下划线为整词, 统一小写
// ---------------------------------------------------------------------------
void TestLocalTokenize::asciiWords()
{
    const vector<string> t = LocalTokenize::tokenize("Hello MySQL Database");
    QCOMPARE(t.size(), static_cast<size_t>(3));
    QCOMPARE(t[0], string("hello"));
    QCOMPARE(t[1], string("mysql"));
    QCOMPARE(t[2], string("database"));
}

// ---------------------------------------------------------------------------
// 中文: UTF-8 2-gram 滑窗 → "数据库连接失败" 应含 "数据" / "连接" / "失败"
// ---------------------------------------------------------------------------
void TestLocalTokenize::chineseNGram()
{
    const vector<string> t = LocalTokenize::tokenize("数据库连接失败");
    QVERIFY(has(t, string("数据")));
    QVERIFY(has(t, string("连接")));
    QVERIFY(has(t, string("失败")));
}

// ---------------------------------------------------------------------------
// 停用词过滤: "这个" 是停用词 → 被丢弃, "数据" 保留
// ---------------------------------------------------------------------------
void TestLocalTokenize::stopWordsRemoved()
{
    const vector<string> t = LocalTokenize::tokenize("这个是数据库");
    QVERIFY(!has(t, string("这个")));
    QVERIFY(has(t, string("数据")));
    QVERIFY(LocalTokenize::isStopWord("the"));
    QVERIFY(!LocalTokenize::isStopWord("mysql"));
}

// ---------------------------------------------------------------------------
// 空输入 → 空结果 (调用方无需特判)
// ---------------------------------------------------------------------------
void TestLocalTokenize::emptyInput()
{
    const vector<string> t = LocalTokenize::tokenize("");
    QCOMPARE(t.size(), static_cast<size_t>(0));
}
