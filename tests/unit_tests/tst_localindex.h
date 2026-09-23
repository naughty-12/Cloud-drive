#ifndef TST_LOCALINDEX_H
#define TST_LOCALINDEX_H

/**
 * @file tst_localindex.h
 * @brief LocalTextIndex 单元测试 — 内存倒排索引 (词元 → 文档词频)。
 *
 * 覆盖:
 *   - 倒排召回: candidateFiles(查询词元) → 含任一词元的文档集合 (并集)
 *   - 删除文档: removeDocument 同步清理倒排与前向索引, 重复删除安全
 *   - 重复添加同一 fileId: 先移除旧项再重建 (重新索引/覆盖语义)
 *   - 词频: termFreq(fileId, term) 返回该词在文档中的计数, 不含时为空
 *   - 统计: docCount (文档总数) / docFreq (含该词的文档数, 供 TF-IDF 的 IDF)
 */

#include <QtTest/QtTest>

class TestLocalIndex : public QObject
{
    Q_OBJECT

private slots:
    void addAndRecall();            // 含查询词的文档被召回, 不含的不召回
    void candidateFilesUnions();    // 多词查询 → 各词命中文档的并集; 空查询 → 空集
    void removeDoc();               // 删文档后不再被召回
    void removeUnknownDocIsSafe();  // 删除不存在 / 重复删除不崩溃、不残留
    void reAddReplacesOldContent(); // 同一 fileId 重复 add → 旧内容被替换, 文档数不重复计
    void termFreqCorrect();         // termFreq 返回该词在文档内的计数
    void termFreqMissingTerm();     // 文档不含该词 / 文档不存在 → 空 map
    void docFreqAndDocCount();      // docFreq = 含词文档数, docCount = 文档总数
};

#endif // TST_LOCALINDEX_H
