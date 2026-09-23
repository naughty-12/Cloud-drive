#ifndef TST_TXTEXTRACTOR_H
#define TST_TXTEXTRACTOR_H

/**
 * @file tst_txtextractor.h
 * @brief TxtTextExtractor 单元测试 — 可插拔解析器接口的纯文本族实现。
 *
 * 覆盖:
 *   - 扩展名白名单判定 (文本族 true / 二进制族 false), 大小写不敏感
 *   - 剥离 UTF-8 BOM
 *   - 超长内容截断到 64KB (资源上限)
 *   - 接口多态调用 (经 ITextExtractor* 使用)
 */

#include <QtTest/QtTest>

class TestTxtExtractor : public QObject
{
    Q_OBJECT

private slots:
    void handlesTextFiles();      // 文本族扩展名 → true, 二进制族 → false
    void handlesCaseInsensitive();// 扩展名大小写不敏感
    void extractStripsBom();      // UTF-8 BOM 被剥离
    void extractKeepsPlainText(); // 普通文本原样返回
    void extractTruncatesHuge();  // 200KB → <= 64KB
    void usableThroughInterface();// 经 ITextExtractor* 多态调用
};

#endif // TST_TXTEXTRACTOR_H
