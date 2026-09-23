/**
 * @file LocalTokenize.cpp
 * @brief LocalTokenize 实现 —— ASCII 整词 + UTF-8 中文 2-gram + 停用词过滤。
 */

#include "LocalTokenize.h"

#include <cctype>
#include <set>

namespace {

/// 内置停用词表 (中英常用虚词)。
const std::set<std::string>& stopWords()
{
    static const std::set<std::string> s = {
        "的", "了", "是", "在", "和", "与", "及", "或", "一个", "我们", "你们", "他们",
        "这个", "那个",
        "the", "a", "an", "and", "or", "to", "of", "in", "on", "is", "are", "be", "it", "for"
    };
    return s;
}

inline bool isAsciiWordChar(unsigned char c)
{
    return std::isalnum(c) != 0 || c == '_';
}

/// 由 UTF-8 首字节推断该字符占用的字节数 (非法首字节按 1 处理)。
inline size_t utf8CharLen(unsigned char lead)
{
    if (lead < 0x80)       return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

} // namespace

namespace LocalTokenize {

bool isStopWord(const std::string& token)
{
    return stopWords().count(token) > 0;
}

std::vector<std::string> tokenize(const std::string& utf8Text)
{
    std::vector<std::string> out;
    std::vector<std::string> hanChars;   // 连续汉字序列 (每个元素 = 一个 3 字节 UTF-8 字符)
    std::string asciiWord;

    // 遇到非词字符 / 序列结束 → 结算当前 ASCII 词
    struct AsciiFlusher {
        std::string& w;
        std::vector<std::string>& out;
        void operator()()
        {
            if (!w.empty()) {
                if (!isStopWord(w)) out.push_back(w);
                w.clear();
            }
        }
    } flushAscii = { asciiWord, out };

    // 汉字序列按 2-gram 滑窗结算
    struct HanFlusher {
        std::vector<std::string>& han;
        std::vector<std::string>& out;
        void operator()()
        {
            for (size_t k = 0; k + 1 < han.size(); ++k) {
                const std::string gram = han[k] + han[k + 1];
                if (!isStopWord(gram)) out.push_back(gram);
            }
            han.clear();
        }
    } flushHan = { hanChars, out };

    size_t i = 0;
    while (i < utf8Text.size()) {
        const unsigned char c = static_cast<unsigned char>(utf8Text[i]);
        if (isAsciiWordChar(c)) {
            flushHan();                                   // ASCII 词打断汉字连续序列
            asciiWord += static_cast<char>(std::tolower(c));
            ++i;
        } else {
            flushAscii();
            const size_t len = utf8CharLen(c);
            // 仅 3 字节序列视为中文汉字 (BMP CJK); 其余多字节字符 (emoji/拉丁扩展) 忽略
            if (len == 3 && i + 3 <= utf8Text.size())
                hanChars.push_back(utf8Text.substr(i, 3));
            i += len;
        }
    }
    flushAscii();
    flushHan();
    return out;
}

} // namespace LocalTokenize
