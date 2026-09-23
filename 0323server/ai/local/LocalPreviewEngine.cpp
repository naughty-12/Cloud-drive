/**
 * @file LocalPreviewEngine.cpp
 * @brief LocalPreviewEngine 实现 —— 词频统计 (关键词) + 热点评分 (关键句) + 首段截断 (摘要)。
 *
 * 复杂度: tokenize 一次 O(content), 句子评分 O(句子数 × 句长), 摘要 O(首段长)。
 * 全部为字符串扫描, 无网络/磁盘/数据库访问 —— 不会超时失败 (设计 5.4)。
 */

#include "LocalPreviewEngine.h"

#include "LocalTokenize.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// UTF-8 / 空白 处理
// ---------------------------------------------------------------------------

/// 由 UTF-8 首字节推断字符占用字节数 (非法首字节按 1 处理)。
inline size_t utf8CharLen(unsigned char lead)
{
    if (lead < 0x80)           return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

inline bool isSpaceChar(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::string trim(const std::string& s)
{
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && isSpaceChar(static_cast<unsigned char>(s[begin])))      ++begin;
    while (end > begin && isSpaceChar(static_cast<unsigned char>(s[end - 1])))    --end;
    return s.substr(begin, end - begin);
}

/// 连续空白 (含换行) 折叠成单个空格 —— 摘要要在客户端弹窗里单行可读。
std::string collapseWhitespace(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    bool pendingSpace = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (isSpaceChar(c)) {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out += ' ';
            pendingSpace = false;
        }
        out += static_cast<char>(c);        // 多字节字符 (≥0x80) 非空白, 原样copy
    }
    return out;
}

/// 按 UTF-8 字符截断 (maxChars 个字符, 绝不切碎多字节字符; 尾部残缺字节直接丢弃)。
std::string truncateChars(const std::string& s, size_t maxChars)
{
    size_t i = 0;
    for (size_t chars = 0; chars < maxChars && i < s.size(); ++chars) {
        const size_t len = utf8CharLen(static_cast<unsigned char>(s[i]));
        if (i + len > s.size())
            break;                          // 残缺序列不输出, 保证结果仍是合法 UTF-8
        i += len;
    }
    return s.substr(0, i);
}

// ---------------------------------------------------------------------------
// 文件名 → 文件类型 (扩展名表; 与 LocalTagEngine 的规则表互相独立)
// ---------------------------------------------------------------------------

/// 小写扩展名 (含点); 无扩展名 (含 ".gitignore" 这类点开头文件) 返回空串。
std::string extOf(const std::string& fileName)
{
    const size_t sep = fileName.find_last_of("/\\");
    const std::string name = (sep == std::string::npos) ? fileName : fileName.substr(sep + 1);
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0)
        return std::string();

    std::string ext = name.substr(dot);
    for (size_t i = 0; i < ext.size(); ++i)
        ext[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(ext[i])));
    return ext;
}

template <size_t N>
bool extEquals(const std::string& ext, const char* const (&list)[N])
{
    for (size_t i = 0; i < N; ++i)
        if (ext == list[i])
            return true;
    return false;
}

std::string fileTypeOf(const std::string& fileName)
{
    static const char* const kCxxExts[]  = { ".cpp", ".cc", ".cxx", ".h", ".hpp", ".hh" };
    static const char* const kDocExts[]  = { ".md", ".markdown", ".txt" };
    static const char* const kConfExts[] = { ".json", ".conf", ".ini", ".yaml", ".yml", ".xml" };

    const std::string ext = extOf(fileName);
    if (extEquals(ext, kCxxExts))  return "C++ 源码";
    if (extEquals(ext, kDocExts))  return "文档";
    if (extEquals(ext, kConfExts)) return "配置文件";
    return "文本";                          // 计划口径: 其余一律"文本" (含无扩展名)
}

// ---------------------------------------------------------------------------
// 词频统计 (关键词)
// ---------------------------------------------------------------------------

/// 一次 tokenize 的统计结果 (关键词与关键句共用, 避免重复分词)。
struct TermStats
{
    std::vector<std::string> order;                 ///< 词元按首次出现顺序 (同频排序用)
    std::map<std::string, size_t> counts;           ///< 词元 → 频次
    std::map<std::string, size_t> firstSeen;        ///< 词元 → 首次出现序号
};

TermStats collectTerms(const std::string& content)
{
    TermStats stats;
    const std::vector<std::string> tokens = LocalTokenize::tokenize(content);
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::map<std::string, size_t>::iterator it = stats.counts.find(tokens[i]);
        if (it == stats.counts.end()) {
            stats.counts[tokens[i]] = 1;
            stats.firstSeen[tokens[i]] = i;
            stats.order.push_back(tokens[i]);
        } else {
            ++it->second;
        }
    }
    return stats;
}

/// 频次降序; 同频按首次出现顺序 (结果确定可复现, 与 LocalSearchEngine 的排序口径一致)。
struct TermOrder
{
    const TermStats* stats;
    bool operator()(const std::string& a, const std::string& b) const
    {
        const size_t ca = stats->counts.find(a)->second;
        const size_t cb = stats->counts.find(b)->second;
        if (ca != cb)
            return ca > cb;
        return stats->firstSeen.find(a)->second < stats->firstSeen.find(b)->second;
    }
};

std::vector<std::string> topTerms(const TermStats& stats, size_t topN)
{
    std::vector<std::string> terms = stats.order;
    TermOrder order;
    order.stats = &stats;
    std::sort(terms.begin(), terms.end(), order);
    if (terms.size() > topN)
        terms.resize(topN);
    return terms;
}

// ---------------------------------------------------------------------------
// 切句 / 段落
// ---------------------------------------------------------------------------

/// 按 。！？!? 与换行切句。句末中文标点保留在句内 (便于客户端直接显示),
/// 换行与英文句点同样作为分隔符; 空句/纯空白句跳过。
std::vector<std::string> splitSentences(const std::string& text)
{
    std::vector<std::string> sentences;
    std::string cur;

    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\n' || c == '\r') {
            const std::string one = trim(cur);
            if (!one.empty())
                sentences.push_back(one);
            cur.clear();
            ++i;
            continue;
        }

        const size_t len = utf8CharLen(c);
        if (i + len > text.size()) {
            cur += text.substr(i);          // 残缺尾部: 原样收下, 不再当分隔符处理
            break;
        }
        const std::string ch = text.substr(i, len);
        cur += ch;
        if (ch == "。" || ch == "！" || ch == "？" || ch == "." || ch == "!" || ch == "?") {
            const std::string one = trim(cur);
            if (!one.empty())
                sentences.push_back(one);
            cur.clear();
        }
        i += len;
    }

    const std::string tail = trim(cur);
    if (!tail.empty())
        sentences.push_back(tail);
    return sentences;
}

/// 首段: 以空行 (仅含空白的行) 为界; 段内多行合并为一行 (换行 → 空格)。
/// 无空行的全文即"一段"; 开头空行被跳过。
std::string firstParagraph(const std::string& text)
{
    std::string para;
    bool started = false;

    size_t i = 0;
    while (i <= text.size()) {
        const size_t nl = text.find('\n', i);
        const bool last = (nl == std::string::npos);
        const std::string line = trim(last ? text.substr(i) : text.substr(i, nl - i));

        if (line.empty()) {
            if (started)
                break;                      // 空行 = 段落结束
        } else {
            if (started)
                para += ' ';
            para += line;
            started = true;
        }

        if (last)
            break;
        i = nl + 1;
    }
    return para;
}

// ---------------------------------------------------------------------------

std::string join(const std::vector<std::string>& parts, char sep)
{
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0)
            out += sep;
        out += parts[i];
    }
    return out;
}

/// 关键句排序: 命中热点词多的在前; 同分保持原文顺序 (结果确定, 不受 sort 实现影响)。
struct ScoreOrder
{
    bool operator()(const std::pair<size_t, size_t>& a, const std::pair<size_t, size_t>& b) const
    {
        if (a.second != b.second)
            return a.second > b.second;
        return a.first < b.first;
    }
};

} // namespace

LocalPreviewResult LocalPreviewEngine::preview(const std::string& content, const std::string& fileName)
{
    LocalPreviewResult result;
    result.fileType = fileTypeOf(fileName);     // 只看文件名, 无内容也能给出

    if (content.empty())
        return result;

    // ---- 关键词: 词频 TopN, 逗号分隔 ----
    const TermStats stats = collectTerms(content);
    const std::vector<std::string> keywords = topTerms(stats, kKeywordTopN);
    result.keywords = join(keywords, ',');

    // ---- 关键句: 含热点词最多的 1~3 句 ----
    // 热点词 = TopN 中频次 ≥ 2 的词元; 一个都没有 (极短文本) 时退化为整个 TopN。
    std::set<std::string> hot;
    for (size_t i = 0; i < keywords.size(); ++i) {
        if (stats.counts.find(keywords[i])->second >= 2)
            hot.insert(keywords[i]);
    }
    if (hot.empty())
        hot.insert(keywords.begin(), keywords.end());

    const std::vector<std::string> sentences = splitSentences(content);
    std::vector<std::pair<size_t, size_t> > scored;         // (句序号, 命中热点词个数)
    for (size_t i = 0; i < sentences.size(); ++i) {
        const std::vector<std::string> sentenceTokens = LocalTokenize::tokenize(sentences[i]);
        std::set<std::string> seenInSentence;               // 同一热点词在一句内只计一次
        size_t score = 0;
        for (size_t k = 0; k < sentenceTokens.size(); ++k) {
            if (hot.count(sentenceTokens[k]) > 0 && seenInSentence.insert(sentenceTokens[k]).second)
                ++score;
        }
        if (score > 0)
            scored.push_back(std::make_pair(i, score));
    }

    std::vector<std::string> picked;
    if (!scored.empty()) {
        // 分数降序; 同分保持原文顺序 → 取前 kMaxKeySentences 句, 再按原文顺序输出
        std::sort(scored.begin(), scored.end(), ScoreOrder());
        size_t cap = kMaxKeySentences;
        if (scored.size() < cap)
            cap = scored.size();

        std::vector<size_t> indices;
        for (size_t i = 0; i < cap; ++i)
            indices.push_back(scored[i].first);
        std::sort(indices.begin(), indices.end());
        for (size_t i = 0; i < indices.size(); ++i)
            picked.push_back(sentences[indices[i]]);
    } else if (!sentences.empty()) {
        picked.push_back(sentences[0]);                      // 兜底: 无热点句 → 首句 (保证非空)
    }
    result.keySentences = join(picked, '\n');

    // ---- 摘要: 首段, 折叠空白, 限 kSummaryChars 个字符 ----
    result.summary = truncateChars(collapseWhitespace(firstParagraph(content)), kSummaryChars);

    return result;
}
