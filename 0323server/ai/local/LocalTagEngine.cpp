/**
 * @file LocalTagEngine.cpp
 * @brief LocalTagEngine 实现 —— 扩展名规则表 + JSON 词典 (aijson) 子串命中 + 去重保序。
 *
 * 匹配细节:
 *   - 词典触发词在 loadDict 时统一 ASCII 小写化, 被匹配文本 (文件名 + 内容) 同样小写化,
 *     因此 "MySQL" 能命中触发词 "mysql"; 小写化只处理 A-Z 字节, 中文 UTF-8 序列原样保留;
 *   - 触发词用子串匹配 (不做分词), 故 "mysql" 同时命中触发词 "sql" —— 对标签场景是期望行为。
 */

#include "LocalTagEngine.h"

#include "../json.hpp"      // 项目内置轻量 JSON 解析 (aijson), 无第三方依赖

#include <cstddef>
#include <exception>
#include <fstream>
#include <sstream>

namespace {

/// ASCII 小写化 (只动 A-Z; 字节 ≥ 0x80 的多字节 UTF-8 序列原样保留, 不会破坏中文)。
std::string asciiLower(const std::string& s)
{
    std::string out = s;
    for (size_t i = 0; i < out.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(out[i]);
        if (c >= 'A' && c <= 'Z')
            out[i] = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

/// 子串匹配 (needle 必须是已小写化文本; 空 needle 一律视为不命中)。
bool containsNoCase(const std::string& loweredHaystack, const std::string& loweredNeedle)
{
    if (loweredNeedle.empty())
        return false;
    return loweredHaystack.find(loweredNeedle) != std::string::npos;
}

/// 去重后追加 (保序: 先到的标签在前)。
void pushUnique(std::vector<std::string>& tags, const std::string& tag)
{
    for (size_t i = 0; i < tags.size(); ++i) {
        if (tags[i] == tag)
            return;
    }
    tags.push_back(tag);
}

/// 扩展名规则表 (设计 4.3 规则一; 每个扩展名对应唯一标签)。
std::vector<std::string> extRuleTags(const std::string& ext)
{
    struct Rule
    {
        const char* ext;
        const char* tag;
    };

    static const Rule kRules[] = {
        { ".cpp", "C++" }, { ".cc", "C++" }, { ".cxx", "C++" },
        { ".h",   "C++" }, { ".hpp", "C++" }, { ".hh", "C++" },
        { ".md",  "文档" }, { ".markdown", "文档" }, { ".txt", "文档" },
        { ".json", "配置" }, { ".conf", "配置" }, { ".ini", "配置" },
        { ".yaml", "配置" }, { ".yml", "配置" }, { ".xml", "配置" },
        { ".sql", "数据库" },
        { ".py",  "Python" },
        { ".jpg", "图片" }, { ".jpeg", "图片" }, { ".png", "图片" },
        { ".gif", "图片" }, { ".bmp", "图片" }, { ".webp", "图片" }, { ".svg", "图片" }
    };

    std::vector<std::string> tags;
    for (size_t i = 0; i < sizeof(kRules) / sizeof(kRules[0]); ++i) {
        if (ext == kRules[i].ext)
            pushUnique(tags, std::string(kRules[i].tag));
    }
    return tags;
}

} // namespace

std::string LocalTagEngine::extOf(const std::string& fileName)
{
    const size_t sep = fileName.find_last_of("/\\");
    const std::string name = (sep == std::string::npos) ? fileName : fileName.substr(sep + 1);
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0)       // 无扩展名 / 点开头的隐藏文件
        return std::string();

    std::string ext = name.substr(dot);
    for (size_t i = 0; i < ext.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(ext[i]);
        if (c >= 'A' && c <= 'Z')
            ext[i] = static_cast<char>(c - 'A' + 'a');
    }
    return ext;
}

bool LocalTagEngine::loadDict(const std::string& jsonPath)
{
    std::ifstream in(jsonPath.c_str(), std::ios::binary);
    if (!in.is_open())
        return false;

    std::ostringstream buffer;
    buffer << in.rdbuf();
    // 紧凑与"格式化"(缩进/逗号后空格/换行) 的 JSON 都由 aijson 直接解析 ——
    // 2026-09-23 修复了 ai/json.hpp parseObject 逗号分支漏 skipWS 的缺陷后, 这里不再需要
    // 事先剥离空白 (原先的 stripWhitespaceOutsideStrings 绕过函数已删除, 只留一处真相)。
    const std::string text = buffer.str();

    // 先解析到临时表, 全部成功才替换 —— 失败时保留原有词典 (调用方无状态可回滚)
    std::map<std::string, std::vector<std::string> > parsed;
    try {
        const aijson::Value root = aijson::Value::parse(text);
        if (root.type != aijson::Value::Object)
            return false;

        for (std::map<std::string, aijson::Value>::const_iterator entry = root.objVal.begin();
             entry != root.objVal.end(); ++entry) {
            if (entry->second.type != aijson::Value::Array)
                continue;                           // 容忍非法条目 (值为数组以外的形态直接跳过)

            std::vector<std::string> triggers;
            for (size_t i = 0; i < entry->second.arrVal.size(); ++i) {
                const aijson::Value& item = entry->second.arrVal[i];
                if (item.type == aijson::Value::String && !item.strVal.empty())
                    triggers.push_back(asciiLower(item.strVal));
            }
            if (!triggers.empty())
                parsed[entry->first] = triggers;
        }
    } catch (const std::exception&) {
        return false;                               // JSON 语法非法 (aijson 抛 std::runtime_error)
    }

    m_dict.swap(parsed);
    return true;
}

std::vector<std::string> LocalTagEngine::tagFile(const std::string& content, const std::string& fileName) const
{
    std::vector<std::string> tags;

    // ---- 规则一: 扩展名 ----
    const std::vector<std::string> extTags = extRuleTags(extOf(fileName));
    for (size_t i = 0; i < extTags.size(); ++i)
        pushUnique(tags, extTags[i]);

    // ---- 规则二 / 三: 词典命中 (文件名 + 内容统一小写化后做子串匹配) ----
    const std::string haystack = asciiLower(fileName + "\n" + content);
    for (std::map<std::string, std::vector<std::string> >::const_iterator entry = m_dict.begin();
         entry != m_dict.end(); ++entry) {
        for (size_t i = 0; i < entry->second.size(); ++i) {
            if (containsNoCase(haystack, entry->second[i])) {
                pushUnique(tags, entry->first);     // 已由扩展名规则给出的同名标签在此被去重
                break;
            }
        }
    }

    return tags;
}
