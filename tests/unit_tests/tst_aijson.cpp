/**
 * @file tst_aijson.cpp
 * @brief aijson (ai/json.hpp) 单元测试 — 重点钉住「格式化 JSON 必须能解析」。
 *
 * 测试对象：0323server/ai/json.hpp (header-only, 经 INCLUDEPATH ../../0323server/ai 引入)
 *
 * 修复前的失败形态: parseObject 的 `while (*p == ',') { p++; parseString(); }` 少了一次
 * skipWS(), p 停在逗号后的空格/换行上, parseString 的 expect('"') 抛
 * std::runtime_error("Expected char")。
 *
 * 断言风格: 解析"必须成功"的用例统一走 parseStrict() —— 异常在辅助函数里被捕获并记为
 * 一条失败, 不让它逃出测试函数 (未捕获异常会让 Qt Test 立刻中断整个套件, 后面的用例
 * 一条都不会执行, 也不利于定位是哪段输入挂掉)。
 */

#include "tst_aijson.h"

#include <QtTest/QtTest>

#include "json.hpp"

#include <exception>
#include <string>

using std::string;

namespace {

/// 解析结果 + 是否成功 (失败时已通过 QTest::qFail 记录, 调用方 if (!ok) return;)
struct ParseOutcome
{
    bool ok;
    aijson::Value value;
};

/// 解析"必须成功"的 JSON; 抛异常时记录失败并返回 ok=false。
ParseOutcome parseStrict(const string& text, const char* file, int line)
{
    ParseOutcome outcome;
    outcome.ok = false;
    try {
        outcome.value = aijson::Value::parse(text);
        outcome.ok = true;
    } catch (const std::exception& ex) {
        QTest::qFail((string("解析抛异常: ") + ex.what() + " | 输入: " + text).c_str(), file, line);
    }
    return outcome;
}

/// 解析失败 (抛异常) 时返回 true, 用于断言"必须抛"的路径。
bool parseThrows(const string& text)
{
    try {
        aijson::Value::parse(text);
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// 紧凑 JSON: 无任何空白的基线形态 (修复前后都必须通过)
// ---------------------------------------------------------------------------
void TestAiJson::compactJson()
{
    const ParseOutcome r = parseStrict("{\"a\":1,\"b\":[1,2]}", __FILE__, __LINE__);
    if (!r.ok)
        return;

    QCOMPARE(static_cast<int>(r.value.type), static_cast<int>(aijson::Value::Object));
    QCOMPARE(r.value.getInt("a"), 1);
    QCOMPARE(r.value["b"].size(), static_cast<size_t>(2));
    QCOMPARE(r.value["b"][0].numVal, 1.0);     // 数组元素直接读 numVal (getNumber 是按对象键取值)
    QCOMPARE(r.value["b"][1].numVal, 2.0);
}

// ---------------------------------------------------------------------------
// 核心断言: 缩进 + 冒号后空格 + 逗号后换行的**格式化 JSON**
// (修复前 → std::runtime_error("Expected char"); 修复后 → 正常解析)
// ---------------------------------------------------------------------------
void TestAiJson::formattedJson()
{
    const string text =
        "{\n"
        "  \"a\": 1,\n"
        "  \"b\": [1, 2]\n"
        "}";

    const ParseOutcome r = parseStrict(text, __FILE__, __LINE__);
    if (!r.ok)
        return;

    QCOMPARE(static_cast<int>(r.value.type), static_cast<int>(aijson::Value::Object));
    QCOMPARE(r.value.objVal.size(), static_cast<size_t>(2));
    QCOMPARE(r.value.getInt("a"), 1);
    QCOMPARE(r.value["b"].size(), static_cast<size_t>(2));
    QCOMPARE(r.value["b"][0].numVal, 1.0);
    QCOMPARE(r.value["b"][1].numVal, 2.0);

    // 更多空白位置: 首尾空白 / 键与冒号之间的换行 / 逗号前后的空白 / 值前的换行
    const string loose = "  \n\t{\"a\"\n:\n\"x\" ,\n\t\"b\" :\t[ 1 , 2 ]\n}\n  ";
    const ParseOutcome r2 = parseStrict(loose, __FILE__, __LINE__);
    if (!r2.ok)
        return;

    QCOMPARE(r2.value.getString("a"), string("x"));
    QCOMPARE(r2.value["b"].size(), static_cast<size_t>(2));
    QCOMPARE(r2.value["b"][1].numVal, 2.0);
}

// ---------------------------------------------------------------------------
// 制表符: skipWS 覆盖空格/换行/回车/制表, 逐字符类型回归
// ---------------------------------------------------------------------------
void TestAiJson::formattedJsonWithTabs()
{
    const ParseOutcome r = parseStrict("{\t\"a\"\t:\t\"x\"\t,\t\"b\"\t:\ttrue\t}", __FILE__, __LINE__);
    if (!r.ok)
        return;

    QCOMPARE(r.value.getString("a"), string("x"));
    QCOMPARE(static_cast<int>(r.value["b"].type), static_cast<int>(aijson::Value::Bool));
    QVERIFY(r.value["b"].boolVal);
}

// ---------------------------------------------------------------------------
// 数组分支: 逗号后空格 (与对象分支对称) + 嵌套数组 + 空容器
// ---------------------------------------------------------------------------
void TestAiJson::arrayCommaWhitespace()
{
    const ParseOutcome flat = parseStrict("[1, 2, 3]", __FILE__, __LINE__);
    if (!flat.ok)
        return;
    QCOMPARE(flat.value.size(), static_cast<size_t>(3));
    QCOMPARE(flat.value[2].numVal, 3.0);

    const ParseOutcome nested = parseStrict("{ \"x\": [\"p\", \"q\"], \"y\": [[1, 2], [3]] }",
                                           __FILE__, __LINE__);
    if (!nested.ok)
        return;
    QCOMPARE(nested.value["x"].size(), static_cast<size_t>(2));
    QCOMPARE(nested.value["x"][1].strVal, string("q"));      // 数组元素是字符串 → 直接读 strVal
    QCOMPARE(nested.value["y"].size(), static_cast<size_t>(2));
    QCOMPARE(nested.value["y"][0][1].numVal, 2.0);
    QCOMPARE(nested.value["y"][1][0].numVal, 3.0);

    const ParseOutcome empties = parseStrict("{ \"e\": [ ], \"o\": { } }", __FILE__, __LINE__);
    if (!empties.ok)
        return;
    QCOMPARE(empties.value["e"].size(), static_cast<size_t>(0));      // 空数组 (括号内含空白)
    QCOMPARE(empties.value["o"].objVal.size(), static_cast<size_t>(0)); // 空对象 (括号内含空白)
}

// ---------------------------------------------------------------------------
// 业务价值: OpenAI 兼容接口的**真实响应体形态** (缩进 + 多行 + 逗号后换行)
// 取值路径 choices[0].message.content 即 AI 预览摘要的来源
// ---------------------------------------------------------------------------
void TestAiJson::llmResponseSample()
{
    const string body =
        "{\n"
        "  \"id\": \"chatcmpl-0323\",\n"
        "  \"object\": \"chat.completion\",\n"
        "  \"created\": 1758600000,\n"
        "  \"model\": \"deepseek-ai/DeepSeek-V3\",\n"
        "  \"choices\": [\n"
        "    {\n"
        "      \"index\": 0,\n"
        "      \"message\": {\n"
        "        \"role\": \"assistant\",\n"
        "        \"content\": \"MySQL 是关系数据库, 支持索引优化\"\n"
        "      },\n"
        "      \"finish_reason\": \"stop\"\n"
        "    }\n"
        "  ],\n"
        "  \"usage\": {\n"
        "    \"prompt_tokens\": 10,\n"
        "    \"completion_tokens\": 5,\n"
        "    \"total_tokens\": 15\n"
        "  }\n"
        "}\n";

    const ParseOutcome r = parseStrict(body, __FILE__, __LINE__);
    if (!r.ok)
        return;

    QCOMPARE(r.value.getString("model"), string("deepseek-ai/DeepSeek-V3"));
    QCOMPARE(r.value["choices"].size(), static_cast<size_t>(1));
    QCOMPARE(r.value["choices"][0].getString("finish_reason"), string("stop"));
    QCOMPARE(r.value["choices"][0]["message"].getString("role"), string("assistant"));
    QCOMPARE(r.value["choices"][0]["message"].getString("content"),
             string("MySQL 是关系数据库, 支持索引优化"));
    QCOMPARE(r.value["usage"].getInt("total_tokens"), 15);
}

// ---------------------------------------------------------------------------
// 只跳过字符串**外**的空白: 值内与键内的空格/制表符必须原样保留
// ---------------------------------------------------------------------------
void TestAiJson::whitespaceInsideStringsKept()
{
    const ParseOutcome r = parseStrict("{ \"a\": \"x  y\" , \"k 1\": \"v\" }", __FILE__, __LINE__);
    if (!r.ok)
        return;

    QCOMPARE(r.value.getString("a"), string("x  y"));      // 两个连续空格保留
    QCOMPARE(r.value.getString("k 1"), string("v"));       // 键内含空格
    QCOMPARE(r.value.objVal.size(), static_cast<size_t>(2));

    const ParseOutcome tab = parseStrict("{\n  \"a\": \"p\tq\"\n}", __FILE__, __LINE__);
    if (!tab.ok)
        return;
    QCOMPARE(tab.value.getString("a"), string("p\tq"));    // 值内含制表符
}

// ---------------------------------------------------------------------------
// 取值语义: 缺失键回默认值 / null 类型 / bool / 负数与小数 / 越界下标
// ---------------------------------------------------------------------------
void TestAiJson::valueTypesAndDefaults()
{
    const ParseOutcome r = parseStrict(
        "{ \"n\": null, \"t\": true, \"f\": false, \"i\": -42, \"d\": 2.5 }", __FILE__, __LINE__);
    if (!r.ok)
        return;

    QVERIFY(r.value.getString("missing").empty());                  // 缺失键 → 默认值
    QCOMPARE(r.value.getString("missing", "def"), string("def"));
    QCOMPARE(r.value.getInt("missing", 7), 7);

    QCOMPARE(static_cast<int>(r.value["n"].type), static_cast<int>(aijson::Value::Null));
    QVERIFY(r.value["t"].boolVal);
    QVERIFY(!r.value["f"].boolVal);
    QCOMPARE(r.value.getInt("i"), -42);
    QCOMPARE(r.value.getNumber("d"), 2.5);

    // 下标越界 → 静态空值 (不崩溃), 供上层安全探测
    QCOMPARE(static_cast<int>(r.value["t"][3].type), static_cast<int>(aijson::Value::Null));
}

// ---------------------------------------------------------------------------
// 非法 JSON 仍必须抛异常: LocalTagEngine::loadDict 靠"抛 → catch → 返回 false"
// 判定词典加载失败, 修复空白容忍度**不得**顺带变成"静默接受垃圾输入"
// ---------------------------------------------------------------------------
void TestAiJson::invalidJsonStillThrows()
{
    QVERIFY(parseThrows("{ this is not json"));            // 对象成员缺引号
    QVERIFY(parseThrows("{\"a\" 1}"));                     // 缺冒号
    QVERIFY(parseThrows("{\"a\": 1,}"));                   // 尾随逗号 (键位置上遇到 '}')
    QVERIFY(parseThrows("{\"a\": [1, 2}"));                // 数组未闭合
    QVERIFY(parseThrows("{\"a\": tru}"));                  // 非法字面量

    // 空白本身不是错误: 空串 / 纯空白 → Null (上层据 type 判定"没有内容")
    QCOMPARE(static_cast<int>(aijson::Value::parse("").type), static_cast<int>(aijson::Value::Null));
    QCOMPARE(static_cast<int>(aijson::Value::parse("  \n\t ").type),
             static_cast<int>(aijson::Value::Null));
}
