#ifndef TST_LOCALTAG_H
#define TST_LOCALTAG_H

/**
 * @file tst_localtag.h
 * @brief LocalTagEngine 单元测试 — 规则 + 词典标签引擎。
 *
 * 覆盖:
 *   - 扩展名规则 (.cpp/.h → C++, .md/.txt → 文档, .json/.conf → 配置, .sql → 数据库,
 *     .py → Python, 图片扩展 → 图片)
 *   - 词典命中 (JSON: tag → 触发词数组; 大小写不敏感的子串匹配)
 *   - 文件名触发词兜底 (规则三) / 扩展名标签与词典标签去重且保序
 *   - 无命中 → 空结果 (不输出"未分类")
 *   - 词典文件缺失 / JSON 非法 → loadDict 返回 false, 扩展名规则仍可用 (不崩溃)
 */

#include <QtTest/QtTest>

class TestLocalTag : public QObject
{
    Q_OBJECT

private slots:
    void extRules();                    // 计划用例: .cpp → C++
    void extRulesCoverage();            // 扩展名规则表全覆盖 + 完整路径/大写扩展名
    void dictHit();                     // 计划用例: 内容含 mysql/数据库 → 数据库
    void dictCaseInsensitive();         // MySQL/TCP 大写形态同样命中
    void dictPathIsResolvable();        // 词典部署到位 (相对副本或源码绝对路径)
    void loadDictToleratesFormattedJson(); // 缩进/逗号后空格的常规 JSON 也能解析
    void fileNameTriggerFallback();     // 规则三: 内容为空, 仅文件名含触发词
    void dedupAndPriority();            // 扩展名标签优先, 同一标签只出现一次
    void noMatchReturnsEmpty();         // 无命中不输出"未分类"
    void emptyContentKeepsExtTag();     // 空内容仍按扩展名给标签
    void loadDictFailureIsSafe();       // 词典缺失 / JSON 非法 → false 且引擎仍可用
};

#endif // TST_LOCALTAG_H
