/**
 * @file tst_localsearch.cpp
 * @brief LocalSearchEngine 单元测试 — 倒排召回 + TF-IDF 余弦排序。
 *
 * 测试对象：0323server/ai/local/LocalSearchEngine.h/cpp (配合 LocalTextIndex)
 */

#include "tst_localsearch.h"

#include <QtTest/QtTest>

#include "LocalSearchEngine.h"
#include "LocalTextIndex.h"

#include <cmath>
#include <string>
#include <vector>

using std::string;
using std::vector;

namespace {

bool nearly(double a, double b, double eps)
{
    return std::fabs(a - b) <= eps;
}

} // namespace

// ---------------------------------------------------------------------------
// 计划用例: 命中查询词更多的文档排前
// ---------------------------------------------------------------------------
void TestLocalSearch::ranksRelevantFirst()
{
    LocalTextIndex idx;
    idx.addDocument(1, "mysql database design book");
    idx.addDocument(2, "cooking recipes daily");

    LocalSearchEngine eng(&idx);
    const vector<LocalSearchEngine::Hit> hits = eng.search("mysql database", 5);

    QVERIFY(!hits.empty());
    QCOMPARE(hits[0].fileId, static_cast<int64_t>(1));
    QVERIFY(hits[0].score > 0.0);
}

// ---------------------------------------------------------------------------
// 严格版: 少命中词的文档 fileId 更小 → 若实现退化成"按 id 排序"或"只数命中词个数"
// 就会失败; 只有真实 TF-IDF 余弦才会让文档 2 胜出。
// doc1 "mysql basics" 只有 1 个查询词且文档短; doc2 命中 2 个查询词。
// ---------------------------------------------------------------------------
void TestLocalSearch::moreMatchedTermsRankHigher()
{
    LocalTextIndex idx;
    idx.addDocument(1, "mysql basics");
    idx.addDocument(2, "mysql database design book");

    LocalSearchEngine eng(&idx);
    const vector<LocalSearchEngine::Hit> hits = eng.search("mysql database", 5);

    QCOMPARE(hits.size(), static_cast<size_t>(2));
    QCOMPARE(hits[0].fileId, static_cast<int64_t>(2));
    QCOMPARE(hits[1].fileId, static_cast<int64_t>(1));
    QVERIFY(hits[0].score > hits[1].score);
    QVERIFY(hits[0].score <= 1.0 + 1e-9);   // 余弦上界
}

// ---------------------------------------------------------------------------
// 计划用例: 无命中 → 空
// ---------------------------------------------------------------------------
void TestLocalSearch::returnsEmptyOnNoMatch()
{
    LocalTextIndex idx;
    idx.addDocument(1, "abc def");

    LocalSearchEngine eng(&idx);
    QVERIFY(eng.search("zzz-not-exist", 5).empty());
}

// ---------------------------------------------------------------------------
// 计划用例: 中文 2-gram 命中 ("数据库" → 词元 "数据"/"据库" 命中该文档)
// ---------------------------------------------------------------------------
void TestLocalSearch::chineseContentHit()
{
    LocalTextIndex idx;
    idx.addDocument(1, "数据库连接池设计");

    LocalSearchEngine eng(&idx);
    const vector<LocalSearchEngine::Hit> hits = eng.search("数据库", 5);

    QVERIFY(!hits.empty());
    QCOMPARE(hits[0].fileId, static_cast<int64_t>(1));
    QVERIFY(hits[0].score > 0.0);
}

// ---------------------------------------------------------------------------
// 降序 + topN 语义
// ---------------------------------------------------------------------------
void TestLocalSearch::scoresDescendAndTopNLimit()
{
    LocalTextIndex idx;
    idx.addDocument(1, "mysql");
    idx.addDocument(2, "mysql database");
    idx.addDocument(3, "mysql database index design");

    LocalSearchEngine eng(&idx);

    const vector<LocalSearchEngine::Hit> all = eng.search("mysql database", 10);
    QCOMPARE(all.size(), static_cast<size_t>(3));
    QCOMPARE(all[0].fileId, static_cast<int64_t>(2));         // 两个查询词都命中的文档最优
    for (size_t i = 1; i < all.size(); ++i)
        QVERIFY(all[i - 1].score >= all[i].score);          // 分数非增

    const vector<LocalSearchEngine::Hit> top2 = eng.search("mysql database", 2);
    QCOMPARE(top2.size(), static_cast<size_t>(2));
    QCOMPARE(top2[0].fileId, all[0].fileId);
    QCOMPARE(top2[1].fileId, all[1].fileId);

    QVERIFY(eng.search("mysql database", 0).empty());        // topN=0 → 不返回任何结果
}

// ---------------------------------------------------------------------------
// 精确分数: 钉住算法本身 (IDF/TF/余弦的取法), 防后续重构静默改变相关性。
//
// 索引: doc1 = "alpha beta", doc2 = "beta gamma"  → N = 2, df(alpha)=1, df(beta)=2
// idf(alpha) = log(3/2)+1 = 1.4054651;  idf(beta) = log(3/3)+1 = 1
// 查询 "alpha beta" (查询侧按词频, 各 1 次):
//   doc1 向量 = (1.4054651, 1.0) → |d1| = 1.7249152
//   dot = 1.4054651*1 + 1.0*1 = 2.4054651;  |q| = sqrt(2) = 1.4142136
//   cos1 = 2.4054651 / (1.4142136 * 1.7249152) = 0.98609
//   doc2 只共享 "beta" → dot = 1.0 → cos2 = 1.0/(1.4142136*1.7249152) = 0.40994
//
// 若文档范数只统计"查询词那几维"(常见偷懒实现) 或查询侧也乘 IDF, 本用例会失败。
// ---------------------------------------------------------------------------
void TestLocalSearch::cosineMatchesDocumentedFormula()
{
    LocalTextIndex idx;
    idx.addDocument(1, "alpha beta");
    idx.addDocument(2, "beta gamma");

    LocalSearchEngine eng(&idx);
    const vector<LocalSearchEngine::Hit> hits = eng.search("alpha beta", 5);

    QCOMPARE(hits.size(), static_cast<size_t>(2));
    QCOMPARE(hits[0].fileId, static_cast<int64_t>(1));
    QCOMPARE(hits[1].fileId, static_cast<int64_t>(2));
    QVERIFY(nearly(hits[0].score, 0.98609, 1e-3));
    QVERIFY(nearly(hits[1].score, 0.40994, 1e-3));
}

// ---------------------------------------------------------------------------
// 同分 → fileId 升序, 结果确定可复现 (相同内容的两个文档)
// ---------------------------------------------------------------------------
void TestLocalSearch::equalScoresBreakTieByFileId()
{
    LocalTextIndex idx;
    idx.addDocument(7, "mysql database");
    idx.addDocument(3, "mysql database");

    LocalSearchEngine eng(&idx);
    const vector<LocalSearchEngine::Hit> hits = eng.search("mysql", 5);

    QCOMPARE(hits.size(), static_cast<size_t>(2));
    QVERIFY(nearly(hits[0].score, hits[1].score, 1e-12));
    QCOMPARE(hits[0].fileId, static_cast<int64_t>(3));   // 小 id 在前
    QCOMPARE(hits[1].fileId, static_cast<int64_t>(7));
}

// ---------------------------------------------------------------------------
// 边界: 空查询 / 空索引 / 空指针索引 → 空结果, 不崩溃
// (服务器集成期索引可能尚未初始化, search 必须能安全返回空)
// ---------------------------------------------------------------------------
void TestLocalSearch::emptyQueryEmptyIndexNullIndex()
{
    LocalTextIndex idx;
    idx.addDocument(1, "mysql");

    LocalSearchEngine eng(&idx);
    QVERIFY(eng.search("", 5).empty());                      // 空查询
    QVERIFY(eng.search("   ", 5).empty());                   // 纯空白查询
    QVERIFY(eng.search("the and", 5).empty());               // 全是停用词 → 无词元

    LocalTextIndex emptyIdx;
    LocalSearchEngine emptyEng(&emptyIdx);
    QVERIFY(emptyEng.search("mysql", 5).empty());            // 空索引

    LocalSearchEngine nullEng(0);
    QVERIFY(nullEng.search("mysql", 5).empty());             // 索引未接入
}
