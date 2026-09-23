#ifndef TST_LOCALPREVIEW_H
#define TST_LOCALPREVIEW_H

/**
 * @file tst_localpreview.h
 * @brief LocalPreviewEngine 单元测试 — 文本统计式预览。
 *
 * 覆盖:
 *   - 三项产出 (摘要/关键词/关键句) 对非空内容均非空
 *   - 扩展名 → 文件类型推断
 *   - 关键词 = 词频 TopN (逗号分隔, 频次降序)
 *   - 关键句 = 含热点词最多的 1~3 句 (换行分隔, 保原文顺序)
 *   - 摘要 = 首段 (空白折叠 + 按 UTF-8 字符截断, 不切碎汉字)
 *   - 空内容 / 全停用词内容的降级行为 (不崩溃, 关键句兜底非空)
 */

#include <QtTest/QtTest>

class TestLocalPreview : public QObject
{
    Q_OBJECT

private slots:
    void returnsNonEmpty();                 // 计划用例: 三项产出均非空
    void fileTypeInferred();                // 计划用例: .cpp → C++ 源码, .txt → 文档
    void keywordsAreTopFrequency();         // 词频降序 + 逗号分隔 + TopN 上限
    void keySentencesPrefersHotSentences(); // 含热点词最多的句子入选, 无关句被排除
    void keySentencesLimitedToThree();      // 上限 3 句
    void summaryIsFirstParagraph();         // 首段优先 + 空白折叠 + 200 字符截断
    void summaryTruncationIsUtf8Safe();     // 截断落在字符边界 (不产生半个汉字)
    void emptyAndStopWordOnlyContent();     // 空内容不崩溃; 全停用词 → 关键句仍非空
};

#endif // TST_LOCALPREVIEW_H
