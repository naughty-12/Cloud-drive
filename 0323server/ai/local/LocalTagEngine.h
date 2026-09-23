#ifndef LOCALTAGENGINE_H
#define LOCALTAGENGINE_H

/**
 * @file LocalTagEngine.h
 * @brief 规则 + 词典标签引擎 —— 自动标签的**主路径** (纯本地, 无网络、无数据库)。
 *
 * 定位 (见 .dsh-dev/specs/2026-09-07-local-engine-first-design.md 4.3):
 *   三级规则取并集, 去重保序:
 *     1. 扩展名规则 (内置表): .cpp/.h → C++, .md/.txt → 文档, .json/.conf → 配置,
 *        .sql → 数据库, .py → Python, 图片扩展 → 图片;
 *     2. 词典规则: 内容命中 tag_dict.json 的触发词 → 该标签
 *        ({"标签": ["触发词", ...]}; ASCII 大小写不敏感, 触发词子串匹配);
 *     3. 文件名兜底: 文件名同样参与词典匹配 (如 mysql_notes.txt → 数据库)。
 *
 * 关键取舍:
 *   - **无命中就返回空**, 不输出"未分类"占位标签 (设计 4.3) —— 标签云只展示有信息的标签;
 *   - 词典加载失败 (文件缺失 / JSON 非法) 不改变原有词典, 引擎退化为纯扩展名规则仍可用,
 *     调用方无需处理异常 (loadDict 返回 false 即可, 不抛);
 *   - 只依赖 C++11 标准库 + 项目内置的 aijson (ai/json.hpp), 零新增第三方依赖。
 *
 * 线程安全: loadDict 非线程安全 (启动期调用一次); tagFile 只读词典, 可并发调用。
 */

#include <map>
#include <string>
#include <vector>

class LocalTagEngine
{
public:
    /// 从 JSON 文件加载词典: {"标签": ["触发词", ...], ...}。
    /// 返回 false = 加载失败 (文件不存在 / JSON 非法 / 根不是对象), 此时词典保持不变。
    bool loadDict(const std::string& jsonPath);

    /// 打标签: 扩展名规则 + 词典命中 (文件名 + 内容), 去重保序 (扩展名标签在前)。
    /// 无任何命中 → 空 vector (调用方自行决定"无标签"的展示)。
    std::vector<std::string> tagFile(const std::string& content, const std::string& fileName) const;

    /// 已加载的词典条目数 (0 = 词典未加载)。
    size_t dictSize() const { return m_dict.size(); }

private:
    /// 小写扩展名 (含点); 无扩展名 / 点开头文件 (".gitignore") 返回空串。
    static std::string extOf(const std::string& fileName);

    /// 词典: 标签 → 触发词列表 (触发词在加载时已 ASCII 小写化, 匹配时不再转换)。
    std::map<std::string, std::vector<std::string> > m_dict;
};

#endif // LOCALTAGENGINE_H
