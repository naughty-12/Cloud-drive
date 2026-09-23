/**
 * @file LocalSearchEngine.cpp
 * @brief LocalSearchEngine 实现 —— TF-IDF 加权余弦相似度检索。
 *
 * 复杂度: O(查询词数 × log 词表 + 命中文档数 × 单文档词元数 × log 词表)。
 * IDF 在单次 search 内按词元缓存, 避免同一词在不同文档上重复查询 docFreq。
 */

#include "LocalSearchEngine.h"

#include "LocalTextIndex.h"
#include "LocalTokenize.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace {

/// 分数降序; 同分按 fileId 升序 (保证顺序确定, 便于上层分页/缓存与测试复现)。
bool hitBetter(const LocalSearchEngine::Hit& a, const LocalSearchEngine::Hit& b)
{
    if (a.score != b.score)
        return a.score > b.score;
    return a.fileId < b.fileId;
}

} // namespace

LocalSearchEngine::LocalSearchEngine(LocalTextIndex* index)
    : m_index(index)
{
}

double LocalSearchEngine::idf(size_t docCount, size_t docFreq)
{
    const double n  = static_cast<double>(docCount);
    const double df = static_cast<double>(docFreq);
    return std::log((n + 1.0) / (df + 1.0)) + 1.0;
}

std::vector<LocalSearchEngine::Hit> LocalSearchEngine::search(const std::string& query, size_t topN) const
{
    std::vector<Hit> hits;

    if (m_index == 0 || topN == 0)
        return hits;

    // ---- 查询向量: 词元 → 查询内频次 (重复词合并) ----
    const std::vector<std::string> qTokens = LocalTokenize::tokenize(query);
    if (qTokens.empty())
        return hits;                            // 空查询 / 全停用词

    std::map<std::string, size_t> qTerms;
    for (size_t i = 0; i < qTokens.size(); ++i)
        ++qTerms[qTokens[i]];

    double qNormSquare = 0.0;
    for (std::map<std::string, size_t>::const_iterator it = qTerms.begin(); it != qTerms.end(); ++it) {
        const double w = static_cast<double>(it->second);
        qNormSquare += w * w;
    }

    // ---- 倒排召回 (含任意查询词的文档) ----
    const std::set<int64_t> candidates = m_index->candidateFiles(qTokens);
    if (candidates.empty())
        return hits;

    const size_t docCount = m_index->docCount();
    const double qNorm = std::sqrt(qNormSquare);
    std::map<std::string, double> idfCache;     // 词元 → IDF (单次检索内复用)

    // ---- 逐候选文档算余弦 ----
    for (std::set<int64_t>::const_iterator f = candidates.begin(); f != candidates.end(); ++f) {
        const std::map<std::string, size_t> docTerms = m_index->documentTerms(*f);
        if (docTerms.empty())
            continue;

        double dot = 0.0;
        double docNormSquare = 0.0;
        for (std::map<std::string, size_t>::const_iterator t = docTerms.begin();
             t != docTerms.end(); ++t) {
            double idfValue;
            const std::map<std::string, double>::const_iterator cached = idfCache.find(t->first);
            if (cached != idfCache.end()) {
                idfValue = cached->second;
            } else {
                idfValue = idf(docCount, m_index->docFreq(t->first));
                idfCache[t->first] = idfValue;
            }

            const double weight = static_cast<double>(t->second) * idfValue;   // 文档侧 TF-IDF 权重
            docNormSquare += weight * weight;                                  // 范数需覆盖全部词元

            const std::map<std::string, size_t>::const_iterator q = qTerms.find(t->first);
            if (q != qTerms.end())
                dot += weight * static_cast<double>(q->second);                // 查询侧 = 词频
        }

        if (dot <= 0.0 || docNormSquare <= 0.0)
            continue;

        Hit hit;
        hit.fileId = *f;
        hit.score  = dot / (qNorm * std::sqrt(docNormSquare));
        hits.push_back(hit);
    }

    std::sort(hits.begin(), hits.end(), hitBetter);

    if (hits.size() > topN)
        hits.resize(topN);

    return hits;
}
