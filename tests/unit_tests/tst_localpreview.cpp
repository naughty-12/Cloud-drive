/**
 * @file tst_localpreview.cpp
 * @brief LocalPreviewEngine 单元测试 — 关键词 / 关键句 / 摘要 的本地统计式预览。
 *
 * 测试对象：0323server/ai/local/LocalPreviewEngine.h/cpp
 */

#include "tst_localpreview.h"

#include <QtTest/QtTest>

#include "LocalPreviewEngine.h"

#include <algorithm>
#include <string>
#include <vector>

using std::string;
using std::vector;

namespace {

/// 按单字符分隔符切分 (空串 → 空 vector, 不产生一个空元素)。
vector<string> split(const string& s, char sep)
{
    vector<string> out;
    if (s.empty())
        return out;

    string cur;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += s[i];
        }
    }
    out.push_back(cur);
    return out;
}

bool has(const vector<string>& v, const string& s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}

bool contains(const string& haystack, const string& needle)
{
    return haystack.find(needle) != string::npos;
}

} // namespace

// ---------------------------------------------------------------------------
// 计划用例: 非空内容 → 摘要 / 关键词 / 关键句 均非空, 关键词含高频词 mysql
// ---------------------------------------------------------------------------
void TestLocalPreview::returnsNonEmpty()
{
    const LocalPreviewResult r = LocalPreviewEngine::preview(
        "MySQL 是关系数据库。MySQL 支持索引。我们使用 MySQL 做存储。", "a.md");

    QVERIFY(!r.summary.empty());
    QVERIFY(!r.keywords.empty());
    QVERIFY(!r.keySentences.empty());
    QVERIFY(has(split(r.keywords, ','), string("mysql")));
    QCOMPARE(r.fileType, string("文档"));
}

// ---------------------------------------------------------------------------
// 计划用例: 扩展名 → 文件类型 (完整路径与大写扩展名同样识别)
// ---------------------------------------------------------------------------
void TestLocalPreview::fileTypeInferred()
{
    QCOMPARE(LocalPreviewEngine::preview("x", "a.cpp").fileType, string("C++ 源码"));
    QCOMPARE(LocalPreviewEngine::preview("x", "b.h").fileType, string("C++ 源码"));
    QCOMPARE(LocalPreviewEngine::preview("x", "b.txt").fileType, string("文档"));
    QCOMPARE(LocalPreviewEngine::preview("x", "c.md").fileType, string("文档"));
    QCOMPARE(LocalPreviewEngine::preview("x", "d.json").fileType, string("配置文件"));
    QCOMPARE(LocalPreviewEngine::preview("x", "e.conf").fileType, string("配置文件"));
    QCOMPARE(LocalPreviewEngine::preview("x", "f.bin").fileType, string("文本"));
    QCOMPARE(LocalPreviewEngine::preview("x", "NOEXT").fileType, string("文本"));
    QCOMPARE(LocalPreviewEngine::preview("x", "C:\\disk1\\a.CPP").fileType, string("C++ 源码"));
}

// ---------------------------------------------------------------------------
// 关键词: 频次降序 (mysql×3 → 索引×2 → 优化×1), 逗号分隔, 最多 8 个
// ---------------------------------------------------------------------------
void TestLocalPreview::keywordsAreTopFrequency()
{
    const vector<string> kws = split(
        LocalPreviewEngine::preview("mysql 索引 mysql 索引 mysql 优化", "x.txt").keywords, ',');

    QCOMPARE(kws.size(), static_cast<size_t>(3));
    QCOMPARE(kws[0], string("mysql"));
    QCOMPARE(kws[1], string("索引"));
    QCOMPARE(kws[2], string("优化"));

    // TopN 上限: 10 个同频词 → 只取前 8 个 (同频按首次出现顺序)
    const vector<string> capped = split(
        LocalPreviewEngine::preview("w1 w2 w3 w4 w5 w6 w7 w8 w9 w10", "x.txt").keywords, ',');
    QCOMPARE(capped.size(), static_cast<size_t>(8));
    QCOMPARE(capped[0], string("w1"));
    QCOMPARE(capped[7], string("w8"));
}

// ---------------------------------------------------------------------------
// 关键句: 含热点词最多的句子入选; 与热点无关的句子必须被排除
//   "mysql 数据库设计。" / "mysql 数据库优化。" 各含 mysql+数据+据库 (3 个热点词)
//   "备份恢复。" 不含任何热点词 → 不入选
// ---------------------------------------------------------------------------
void TestLocalPreview::keySentencesPrefersHotSentences()
{
    const LocalPreviewResult r = LocalPreviewEngine::preview(
        "mysql 数据库设计。\nmysql 数据库优化。\n备份恢复。\n", "note.txt");

    const vector<string> sents = split(r.keySentences, '\n');
    QCOMPARE(sents.size(), static_cast<size_t>(2));         // 有多少取多少, 不凑数
    QVERIFY(contains(sents[0], "数据库"));
    QVERIFY(contains(sents[1], "数据库"));
    for (size_t i = 0; i < sents.size(); ++i)
        QVERIFY(!contains(sents[i], "备份"));
}

// ---------------------------------------------------------------------------
// 关键句上限 3 句, 且保持原文顺序 (中间那句与热点无关 → 跳过)
// ---------------------------------------------------------------------------
void TestLocalPreview::keySentencesLimitedToThree()
{
    const LocalPreviewResult r = LocalPreviewEngine::preview(
        "mysql 简介。\n关于天气的无关段落。\nmysql 安装步骤。\nmysql 索引优化。\nmysql 备份恢复。",
        "note.txt");

    const vector<string> sents = split(r.keySentences, '\n');
    QCOMPARE(sents.size(), static_cast<size_t>(3));
    for (size_t i = 0; i < sents.size(); ++i) {
        QVERIFY(contains(sents[i], "mysql"));
        QVERIFY(!contains(sents[i], "无关"));
    }
    QCOMPARE(sents[0], string("mysql 简介。"));              // 同分保持原文顺序
}

// ---------------------------------------------------------------------------
// 摘要: 首段 (空行分隔) + 段内换行折叠为空格 + 不越段
// ---------------------------------------------------------------------------
void TestLocalPreview::summaryIsFirstParagraph()
{
    const LocalPreviewResult r = LocalPreviewEngine::preview(
        "第一段内容。\n仍有第一段。\n\n第二段内容。", "a.md");

    QCOMPARE(r.summary, string("第一段内容。 仍有第一段。"));
    QVERIFY(!contains(r.summary, "第二段"));
}

// ---------------------------------------------------------------------------
// 摘要长度: 按 UTF-8 字符截断 (300 个汉字 → 200 字符 = 600 字节, 不是 200 字节;
// 且必须落在字符边界上, 否则尾部会出现乱码残字节)
// ---------------------------------------------------------------------------
void TestLocalPreview::summaryTruncationIsUtf8Safe()
{
    string han;
    for (int i = 0; i < 300; ++i)
        han += "中";                                        // 每字 3 字节

    const string summary = LocalPreviewEngine::preview(han, "a.txt").summary;
    QCOMPARE(summary.size(), static_cast<size_t>(600));
    QCOMPARE(summary.size() % 3, static_cast<size_t>(0));    // 无残字节 → 未切碎汉字

    const string ascii = LocalPreviewEngine::preview(string(300, 'a'), "a.txt").summary;
    QCOMPARE(ascii.size(), static_cast<size_t>(200));        // ASCII 200 字符 = 200 字节
}

// ---------------------------------------------------------------------------
// 降级: 空内容不崩溃且不产出伪关键词; 全停用词内容的关键句兜底非空
// (设计 5.4: 本地结果永远非空 —— 最差也有首句/文件名类型)
// ---------------------------------------------------------------------------
void TestLocalPreview::emptyAndStopWordOnlyContent()
{
    const LocalPreviewResult empty = LocalPreviewEngine::preview("", "a.md");
    QVERIFY(empty.summary.empty());
    QVERIFY(empty.keywords.empty());
    QVERIFY(empty.keySentences.empty());
    QCOMPARE(empty.fileType, string("文档"));

    QCOMPARE(LocalPreviewEngine::preview("", "").fileType, string("文本"));

    const LocalPreviewResult stops = LocalPreviewEngine::preview("the and or in on", "x.txt");
    QVERIFY(stops.keywords.empty());                         // 全被停用词表过滤
    QCOMPARE(stops.keySentences, string("the and or in on")); // 无热点句 → 兜底首句 (非空)
    QVERIFY(!stops.summary.empty());
}
