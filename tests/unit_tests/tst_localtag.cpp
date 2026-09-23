/**
 * @file tst_localtag.cpp
 * @brief LocalTagEngine 单元测试 — 扩展名规则 + 词典命中 + 去重保序。
 *
 * 测试对象：0323server/ai/local/LocalTagEngine.h/cpp + ai/local/tag_dict.json
 *
 * 词典路径策略 (计划 Task 6 Step 4): unit_tests.pro 用 QMAKE_POST_LINK 把
 * tag_dict.json 拷到构建目录, 故测试优先用**工作目录下的相对副本**;
 * 副本不存在 (例如只拷了 exe 到别处跑) 时回退到编译期写入的源码绝对路径
 * (LOCAL_TAG_DICT_PATH)。两种方式都不依赖外部环境变量。
 */

#include "tst_localtag.h"

#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include "LocalTagEngine.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

using std::string;
using std::vector;

#ifndef LOCAL_TAG_DICT_PATH
#define LOCAL_TAG_DICT_PATH ""
#endif

namespace {

/// tag_dict.json 的实际加载路径: 构建目录副本优先, 否则回退到源码绝对路径。
QString dictPath()
{
    const QString deployed = QDir::currentPath() + QDir::separator() + QString("tag_dict.json");
    if (QFileInfo::exists(deployed))
        return deployed;
    return QString::fromUtf8(LOCAL_TAG_DICT_PATH);
}

bool has(const vector<string>& v, const string& s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}

size_t countOf(const vector<string>& v, const string& s)
{
    return static_cast<size_t>(std::count(v.begin(), v.end(), s));
}

/// 临时写一个词典文件 (测非法 JSON / 缺失文件的分支), 返回路径; 调用方负责 QFile::remove。
QString writeTempDict(const string& content, const QString& name)
{
    const QString path = QDir::tempPath() + QDir::separator() + name;
    std::ofstream out(path.toStdString().c_str(), std::ios::binary | std::ios::trunc);
    out << content;
    out.close();
    return path;
}

} // namespace

// ---------------------------------------------------------------------------
// 计划用例: 扩展名规则 (.cpp → C++)
// ---------------------------------------------------------------------------
void TestLocalTag::extRules()
{
    LocalTagEngine e;
    const vector<string> tags = e.tagFile("some text", "main.cpp");
    QVERIFY(has(tags, string("C++")));
}

// ---------------------------------------------------------------------------
// 扩展名规则表: 计划口径全覆盖 (含完整路径与大写扩展名)
// ---------------------------------------------------------------------------
void TestLocalTag::extRulesCoverage()
{
    LocalTagEngine e;

    QVERIFY(has(e.tagFile("x", "a.h"),     string("C++")));
    QVERIFY(has(e.tagFile("x", "a.md"),    string("文档")));
    QVERIFY(has(e.tagFile("x", "a.txt"),   string("文档")));
    QVERIFY(has(e.tagFile("x", "a.json"),  string("配置")));
    QVERIFY(has(e.tagFile("x", "a.conf"),  string("配置")));
    QVERIFY(has(e.tagFile("x", "a.sql"),   string("数据库")));
    QVERIFY(has(e.tagFile("x", "a.py"),    string("Python")));
    QVERIFY(has(e.tagFile("x", "a.png"),   string("图片")));
    QVERIFY(has(e.tagFile("x", "a.jpg"),   string("图片")));
    QVERIFY(has(e.tagFile("x", "C:\\disk1\\src\\a.CPP"), string("C++")));   // 路径 + 大写

    QVERIFY(e.tagFile("x", "a.bin").empty());        // 未登记扩展名 → 无标签
    QVERIFY(e.tagFile("x", "noext").empty());
}

// ---------------------------------------------------------------------------
// 计划用例: 词典命中 (unit_tests.pro 已把 tag_dict.json 部署到运行目录)
// ---------------------------------------------------------------------------
void TestLocalTag::dictHit()
{
    LocalTagEngine e;
    QVERIFY2(e.loadDict(dictPath().toStdString()), qPrintable(dictPath()));

    const vector<string> tags = e.tagFile("使用 mysql 连接数据库的表结构", "notes.txt");
    QVERIFY(has(tags, string("文档")));      // 扩展名规则
    QVERIFY(has(tags, string("数据库")));    // 词典规则
}

// ---------------------------------------------------------------------------
// 词典匹配大小写不敏感: "MySQL"/"TCP" 与 "mysql"/"tcp" 等价
// ---------------------------------------------------------------------------
void TestLocalTag::dictCaseInsensitive()
{
    LocalTagEngine e;
    QVERIFY(e.loadDict(dictPath().toStdString()));

    const vector<string> tags = e.tagFile("MySQL 存储引擎; TCP SOCKET 编程", "a.txt");
    QVERIFY(has(tags, string("数据库")));
    QVERIFY(has(tags, string("网络")));
}

// ---------------------------------------------------------------------------
// 词典部署可解析 (构建目录副本或源码绝对路径至少有一个存在)
// ---------------------------------------------------------------------------
void TestLocalTag::dictPathIsResolvable()
{
    const QString path = dictPath();
    QVERIFY2(!path.isEmpty(), "LOCAL_TAG_DICT_PATH 未定义且工作目录无 tag_dict.json");
    QVERIFY2(QFileInfo::exists(path), qPrintable(path));
}

// ---------------------------------------------------------------------------
// 词典文件可用常规格式书写 (缩进 + 逗号后空格): 解析前会把字符串外的空白规范化,
// 规避项目内置 aijson 的 parseObject 不在逗号后跳空白 (只吃紧凑 JSON) 的限制。
// ---------------------------------------------------------------------------
void TestLocalTag::loadDictToleratesFormattedJson()
{
    const QString path = writeTempDict(
        "{\n"
        "    \"数据库\": [\"mysql\", \"数据库\"],\n"
        "    \"网络\":  [\"socket\"]\n"
        "}\n", "tst_localtag_formatted.json");

    LocalTagEngine e;
    const bool ok = e.loadDict(path.toStdString());
    QFile::remove(path);
    QVERIFY2(ok, "带缩进/逗号后空格的 JSON 也必须能解析");
    QCOMPARE(e.dictSize(), static_cast<size_t>(2));

    QVERIFY(has(e.tagFile("使用 mysql 存储", "a.txt"), string("数据库")));
    QVERIFY(has(e.tagFile("socket 编程", "a.txt"), string("网络")));
}

// ---------------------------------------------------------------------------
// 规则三: 内容为空, 仅文件名含触发词 → 仍能命中词典
// ---------------------------------------------------------------------------
void TestLocalTag::fileNameTriggerFallback()
{
    LocalTagEngine e;
    QVERIFY(e.loadDict(dictPath().toStdString()));

    const vector<string> tags = e.tagFile("", "mysql_notes.unknown");
    QCOMPARE(tags.size(), static_cast<size_t>(1));
    QCOMPARE(tags[0], string("数据库"));
}

// ---------------------------------------------------------------------------
// 去重 + 优先级: 扩展名标签排在前; 同一标签 (如 .sql 与词典 "数据库") 只出现一次
// ---------------------------------------------------------------------------
void TestLocalTag::dedupAndPriority()
{
    LocalTagEngine e;
    QVERIFY(e.loadDict(dictPath().toStdString()));

    const vector<string> code = e.tagFile("cpp 函数 class", "main.cpp");
    QCOMPARE(code.size(), static_cast<size_t>(2));
    QCOMPARE(code[0], string("C++"));        // 扩展名规则在前
    QCOMPARE(code[1], string("代码"));       // 词典规则在后

    const vector<string> sql = e.tagFile("create table mysql 索引", "schema.sql");
    QCOMPARE(countOf(sql, string("数据库")), static_cast<size_t>(1));   // 规则1 与规则2 撞同一标签 → 去重
}

// ---------------------------------------------------------------------------
// 无命中 → 空结果 (设计 4.3: 兜底"未分类"不输出)
// ---------------------------------------------------------------------------
void TestLocalTag::noMatchReturnsEmpty()
{
    LocalTagEngine e;
    QVERIFY(e.loadDict(dictPath().toStdString()));

    const vector<string> tags = e.tagFile("hello world", "readme.xyz");
    QVERIFY(tags.empty());
    QVERIFY(!has(tags, string("未分类")));
}

// ---------------------------------------------------------------------------
// 空内容: 仍按扩展名给标签 (内容提取失败的兜底路径)
// ---------------------------------------------------------------------------
void TestLocalTag::emptyContentKeepsExtTag()
{
    LocalTagEngine e;

    const vector<string> tags = e.tagFile("", "main.h");
    QCOMPARE(tags.size(), static_cast<size_t>(1));
    QCOMPARE(tags[0], string("C++"));
}

// ---------------------------------------------------------------------------
// 词典加载失败必须安全: 文件不存在 / JSON 非法 → false, 引擎退化为纯扩展名规则
// ---------------------------------------------------------------------------
void TestLocalTag::loadDictFailureIsSafe()
{
    LocalTagEngine e;

    QVERIFY(!e.loadDict("no_such_dict_file_0323.json"));
    QVERIFY(has(e.tagFile("x", "a.md"), string("文档")));           // 词典缺失不影响扩展名规则
    QVERIFY(e.tagFile("使用 mysql 连接数据库的表结构", "notes.md").size() == 1);  // 无词典 → 只剩"文档"

    const QString bad = writeTempDict("{ this is not json", "tst_localtag_bad.json");
    QVERIFY(!e.loadDict(bad.toStdString()));
    QFile::remove(bad);

    // 失败后再次成功加载, 引擎可恢复 (不被上一次失败污染)
    QVERIFY(e.loadDict(dictPath().toStdString()));
    QVERIFY(has(e.tagFile("mysql", "a.txt"), string("数据库")));
}
