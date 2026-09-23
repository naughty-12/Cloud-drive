/**
 * @file tst_localindex.cpp
 * @brief LocalTextIndex 单元测试 — 内存倒排索引 (倒排召回 + 词频统计)。
 *
 * 测试对象：0323server/ai/local/LocalTextIndex.h/cpp
 */

#include "tst_localindex.h"

#include <QtTest/QtTest>

#include "LocalTextIndex.h"
#include "LocalTokenize.h"

#include <map>
#include <set>
#include <string>
#include <vector>

using std::map;
using std::set;
using std::string;
using std::vector;

// ---------------------------------------------------------------------------
// 倒排召回: "mysql" 只出现在文档 1/2 → 文档 3 不得被召回
// ---------------------------------------------------------------------------
void TestLocalIndex::addAndRecall()
{
    LocalTextIndex idx;
    idx.addDocument(1, "hello mysql database");
    idx.addDocument(2, "mysql index design");
    idx.addDocument(3, "unrelated content here");

    const set<int64_t> cand = idx.candidateFiles(LocalTokenize::tokenize("mysql"));
    QCOMPARE(cand.size(), static_cast<size_t>(2));
    QVERIFY(cand.count(1) == 1);
    QVERIFY(cand.count(2) == 1);
    QVERIFY(cand.count(3) == 0);
}

// ---------------------------------------------------------------------------
// 多词召回 = 并集去重; 空查询词列表 → 空召回 (调用方无需特判)
// ---------------------------------------------------------------------------
void TestLocalIndex::candidateFilesUnions()
{
    LocalTextIndex idx;
    idx.addDocument(1, "mysql index");
    idx.addDocument(2, "database index");
    idx.addDocument(3, "unrelated content");

    // "mysql"/"database" 分别命中 1/2, "index" 同时命中 1/2 → 并集仍为 {1,2}
    const set<int64_t> cand = idx.candidateFiles(LocalTokenize::tokenize("mysql database index"));
    QCOMPARE(cand.size(), static_cast<size_t>(2));
    QVERIFY(cand.count(1) == 1);
    QVERIFY(cand.count(2) == 1);

    QVERIFY(idx.candidateFiles(vector<string>()).empty());
}

// ---------------------------------------------------------------------------
// 删除文档后: 不再被召回, 词频/文档数同步归零
// ---------------------------------------------------------------------------
void TestLocalIndex::removeDoc()
{
    LocalTextIndex idx;
    idx.addDocument(1, "hello mysql");
    idx.addDocument(2, "mysql");

    idx.removeDocument(1);

    const set<int64_t> cand = idx.candidateFiles(LocalTokenize::tokenize("mysql"));
    QCOMPARE(cand.size(), static_cast<size_t>(1));
    QVERIFY(cand.count(2) == 1);
    QVERIFY(cand.count(1) == 0);
    QCOMPARE(idx.docCount(), static_cast<size_t>(1));
    QCOMPARE(idx.docFreq("mysql"), static_cast<size_t>(1));
    QVERIFY(idx.termFreq(1, "mysql").empty());
}

// ---------------------------------------------------------------------------
// 幂等性: 删除不存在的文档 / 重复删除同一文档均安全, 不产生残留
// ---------------------------------------------------------------------------
void TestLocalIndex::removeUnknownDocIsSafe()
{
    LocalTextIndex idx;
    idx.addDocument(1, "mysql");

    idx.removeDocument(99);   // 从未索引
    idx.removeDocument(1);
    idx.removeDocument(1);    // 重复删除

    QCOMPARE(idx.docCount(), static_cast<size_t>(0));
    QCOMPARE(idx.docFreq("mysql"), static_cast<size_t>(0));
    QVERIFY(idx.candidateFiles(LocalTokenize::tokenize("mysql")).empty());
}

// ---------------------------------------------------------------------------
// 同一 fileId 重复 addDocument → 先移除旧项再重建 (重新索引语义)
// ---------------------------------------------------------------------------
void TestLocalIndex::reAddReplacesOldContent()
{
    LocalTextIndex idx;
    idx.addDocument(1, "mysql database");
    idx.addDocument(1, "redis cache");          // 覆盖: 旧词元必须消失

    QVERIFY(idx.candidateFiles(LocalTokenize::tokenize("mysql")).empty());
    QVERIFY(idx.candidateFiles(LocalTokenize::tokenize("redis")).count(1) == 1);
    QCOMPARE(idx.docCount(), static_cast<size_t>(1));      // 文档数不重复计
    QCOMPARE(idx.docFreq("mysql"), static_cast<size_t>(0)); // 旧词倒排项已清理
    QVERIFY(idx.termFreq(1, "mysql").empty());
    QVERIFY(idx.termFreq(1, "redis").at("redis") == 1.0);
}

// ---------------------------------------------------------------------------
// 词频: 计划接口 termFreq(fileId, term) → {term: 计数}
// ---------------------------------------------------------------------------
void TestLocalIndex::termFreqCorrect()
{
    LocalTextIndex idx;
    idx.addDocument(1, "mysql mysql database");

    const map<string, double> tf = idx.termFreq(1, "mysql");
    QCOMPARE(tf.size(), static_cast<size_t>(1));
    QVERIFY(tf.at("mysql") == 2.0);
    QVERIFY(tf.at("mysql") != 1.0);             // 计数而非布尔
}

// ---------------------------------------------------------------------------
// 查不到就是空 map (不抛异常, 调用方无需特判)
// ---------------------------------------------------------------------------
void TestLocalIndex::termFreqMissingTerm()
{
    LocalTextIndex idx;
    idx.addDocument(1, "mysql");

    QVERIFY(idx.termFreq(1, "redis").empty());   // 文档不含该词
    QVERIFY(idx.termFreq(99, "mysql").empty());  // 文档不存在
}

// ---------------------------------------------------------------------------
// docFreq (含词文档数, 供 IDF) 与 docCount (文档总数) 随增删同步
// ---------------------------------------------------------------------------
void TestLocalIndex::docFreqAndDocCount()
{
    LocalTextIndex idx;
    QCOMPARE(idx.docCount(), static_cast<size_t>(0));
    QCOMPARE(idx.docFreq("mysql"), static_cast<size_t>(0));

    idx.addDocument(1, "mysql database");
    idx.addDocument(2, "mysql index");
    idx.addDocument(3, "redis cache");

    QCOMPARE(idx.docCount(), static_cast<size_t>(3));
    QCOMPARE(idx.docFreq("mysql"), static_cast<size_t>(2));
    QCOMPARE(idx.docFreq("index"), static_cast<size_t>(1));
    QCOMPARE(idx.docFreq("absent"), static_cast<size_t>(0));

    idx.removeDocument(2);
    QCOMPARE(idx.docCount(), static_cast<size_t>(2));
    QCOMPARE(idx.docFreq("mysql"), static_cast<size_t>(1));
}
