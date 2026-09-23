#ifndef TST_LOCALTOKENIZE_H
#define TST_LOCALTOKENIZE_H

/**
 * @file tst_localtokenize.h
 * @brief LocalTokenize 单元测试 — 本地引擎词元化。
 *
 * 覆盖:
 *   - ASCII 连续字母/数字/下划线按整词保留并小写化
 *   - 中文按 UTF-8 2-gram 滑窗产词元
 *   - 内置中英停用词过滤
 *   - 空串输入
 */

#include <QtTest/QtTest>

class TestLocalTokenize : public QObject
{
    Q_OBJECT

private slots:
    void asciiWords();          // "Hello MySQL Database" → 3 词元, 小写化
    void chineseNGram();        // 中文 2-gram 滑窗命中
    void stopWordsRemoved();    // 停用词 ("这个") 被过滤, 实词保留
    void emptyInput();          // 空串 → 0 词元
};

#endif // TST_LOCALTOKENIZE_H
