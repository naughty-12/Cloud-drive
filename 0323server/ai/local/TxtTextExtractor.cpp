/**
 * @file TxtTextExtractor.cpp
 * @brief TxtTextExtractor 实现 —— 扩展名白名单 + BOM 剥离 + 64KB 截断。
 */

#include "TxtTextExtractor.h"

#include <cctype>
#include <cstring>

// 静态常量成员的类外定义 (C++11: 类内初始化 + 类外定义, 避免取地址时链接失败)
const size_t TxtTextExtractor::kMaxExtract;

namespace {

/// 纯文本族扩展名 (小写; 与 fileName 尾部做不区分大小写比对)。
const char* const kExts[] = {
    ".txt", ".md", ".markdown", ".json", ".log", ".ini", ".conf",
    ".cpp", ".cc", ".c", ".h", ".hpp", ".py", ".sql", ".bat", ".sh",
    ".yaml", ".yml", ".csv", ".xml", ".html", ".htm", ".pro", ".pri"
};

/// s 是否以 suffix (小写) 结尾 —— 不区分大小写。
bool endsWithIgnoreCase(const std::string& s, const char* suffix)
{
    const size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        const char c = s[s.size() - n + i];
        if (static_cast<char>(std::tolower(static_cast<unsigned char>(c))) != suffix[i])
            return false;
    }
    return true;
}

} // namespace

bool TxtTextExtractor::canHandle(const std::string& fileName) const
{
    for (size_t i = 0; i < sizeof(kExts) / sizeof(kExts[0]); ++i) {
        if (endsWithIgnoreCase(fileName, kExts[i]))
            return true;
    }
    return false;
}

std::string TxtTextExtractor::extract(const std::string& rawBytes) const
{
    std::string out = rawBytes;

    // UTF-8 BOM: EF BB BF → 剥离 (否则会污染首个词元)
    if (out.size() >= 3
        && static_cast<unsigned char>(out[0]) == 0xEF
        && static_cast<unsigned char>(out[1]) == 0xBB
        && static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);
    }

    // 资源上限: 超长文件按前缀统计
    if (out.size() > kMaxExtract)
        out.resize(kMaxExtract);

    return out;
}
