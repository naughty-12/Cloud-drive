#ifndef TST_AIJSON_H
#define TST_AIJSON_H

/**
 * @file tst_aijson.h
 * @brief aijson (ai/json.hpp) 单元测试 — 轻量 JSON 解析器的空白容忍度。
 *
 * 背景: 2026-09-23 修复 —— parseObject 的逗号分支在 `p++` 之后不跳过空白,
 * 导致「带空格/缩进/换行的**格式化 JSON**」解析抛 "Expected char";
 * 而 LLM 服务端 (OpenAI 兼容接口) 的响应体常带缩进, 且配置文件也是人写的。
 *
 * 覆盖:
 *   - 紧凑 JSON (回归基线, 修复前后都必须通过)
 *   - 格式化 JSON: 空格 / 换行 / 制表符出现在冒号前后、逗号之后、括号内侧 (核心断言)
 *   - 数组内逗号后带空格 / 嵌套数组
 *   - 真实 LLM 响应样本: choices[0].message.content 取值
 *   - 字符串**内部**的空白必须原样保留 (只跳过字符串外的空白)
 *   - 缺失键 / null / bool / 负数 / 小数
 *   - 非法 JSON 仍抛异常 (LocalTagEngine::loadDict 依赖该契约返回 false)
 */

#include <QtTest/QtTest>

class TestAiJson : public QObject
{
    Q_OBJECT

private slots:
    void compactJson();                 // {"a":1,"b":[1,2]} — 无空白基线
    void formattedJson();               // 核心: 缩进 + 冒号/逗号后空格
    void formattedJsonWithTabs();       // 制表符同样被跳过
    void arrayCommaWhitespace();        // 数组逗号后空格 + 嵌套数组
    void llmResponseSample();           // OpenAI 兼容响应体 (缩进) 取 choices[0].message.content
    void whitespaceInsideStringsKept(); // 字符串内空白不得被吞掉
    void valueTypesAndDefaults();       // 缺失键默认值 / null / bool / 负数 / 小数
    void invalidJsonStillThrows();      // 非法 JSON 仍抛异常 (不静默返回空对象)
};

#endif // TST_AIJSON_H
