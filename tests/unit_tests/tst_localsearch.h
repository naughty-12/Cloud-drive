#ifndef TST_LOCALSEARCH_H
#define TST_LOCALSEARCH_H

/**
 * @file tst_localsearch.h
 * @brief LocalSearchEngine 单元测试 — TF-IDF 加权 + 余弦相似度排序。
 *
 * 覆盖:
 *   - 相关性排序: 命中查询词更多的文档排前 (真实余弦, 非"命中词计数")
 *   - 中文 2-gram 命中: 查询 "数据库" 命中文档 "数据库连接池设计"
 *   - 无命中 / 空查询 / 空索引 / 空指针索引 → 空结果 (不崩溃)
 *   - 降序 + topN 截断 + topN=0
 *   - 分数与文档化公式一致 (IDF = log((N+1)/(df+1))+1; 文档 TF-IDF 向量;
 *     查询词频向量; cos = dot/(|q||d|))
 *   - 同分时按 fileId 升序 (结果确定可复现)
 */

#include <QtTest/QtTest>

class TestLocalSearch : public QObject
{
    Q_OBJECT

private slots:
    void ranksRelevantFirst();            // 命中词更多的文档排前 (计划用例)
    void moreMatchedTermsRankHigher();    // 同上但 fileId 反向, 排除"按 id 排序"的假通过
    void returnsEmptyOnNoMatch();         // 查询词不在任何文档 → 空结果 (计划用例)
    void chineseContentHit();             // 中文 2-gram 命中 (计划用例)
    void scoresDescendAndTopNLimit();     // 降序 + topN 截断 + topN=0 → 空
    void cosineMatchesDocumentedFormula();// 精确分数 pin 住 IDF/TF/余弦公式
    void equalScoresBreakTieByFileId();   // 同分 → fileId 升序 (确定性)
    void emptyQueryEmptyIndexNullIndex(); // 边界输入 → 空结果, 不崩溃
};

#endif // TST_LOCALSEARCH_H
