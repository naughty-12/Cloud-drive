/**
 * @file LocalTextIndex.cpp
 * @brief LocalTextIndex 实现 —— 倒排表 + 前向表 + 文档集合的一致维护。
 *
 * 三个容器的一致性不变量 (每个公开方法退出时都成立):
 *   1. m_docTerms 的键集合 ⊆ m_docs;
 *   2. m_postings[t][f] 存在 ⟺ m_docTerms[f][t] 存在, 且两者计数相等;
 *   3. m_postings 中不存在空的 postings 列表 (删除时同步擦除)。
 */

#include "LocalTextIndex.h"

#include "LocalTokenize.h"

#include <utility>

void LocalTextIndex::addDocument(int64_t fileId, const std::string& utf8Text)
{
    removeDocument(fileId);                 // 覆盖语义: 先清旧项, 再重建

    m_docs.insert(fileId);

    const std::vector<std::string> tokens = LocalTokenize::tokenize(utf8Text);
    if (tokens.empty())
        return;                             // 空内容: 仅登记文档, 不产生倒排项

    std::map<std::string, size_t>& docTerms = m_docTerms[fileId];
    for (size_t i = 0; i < tokens.size(); ++i)
        ++docTerms[tokens[i]];

    for (std::map<std::string, size_t>::const_iterator it = docTerms.begin();
         it != docTerms.end(); ++it)
        m_postings[it->first][fileId] = it->second;
}

void LocalTextIndex::removeDocument(int64_t fileId)
{
    m_docs.erase(fileId);

    const std::map<int64_t, std::map<std::string, size_t> >::iterator docIt = m_docTerms.find(fileId);
    if (docIt == m_docTerms.end())
        return;                             // 无索引项: 幂等返回

    for (std::map<std::string, size_t>::const_iterator t = docIt->second.begin();
         t != docIt->second.end(); ++t) {
        std::map<std::string, std::map<int64_t, size_t> >::iterator post = m_postings.find(t->first);
        if (post == m_postings.end())
            continue;
        post->second.erase(fileId);
        if (post->second.empty())
            m_postings.erase(post);         // 不留空壳: docFreq 才能如实归零
    }
    m_docTerms.erase(docIt);
}

std::map<std::string, double> LocalTextIndex::termFreq(int64_t fileId, const std::string& term) const
{
    std::map<std::string, double> out;

    const std::map<int64_t, std::map<std::string, size_t> >::const_iterator docIt = m_docTerms.find(fileId);
    if (docIt == m_docTerms.end())
        return out;

    const std::map<std::string, size_t>::const_iterator t = docIt->second.find(term);
    if (t != docIt->second.end())
        out[term] = static_cast<double>(t->second);

    return out;
}

std::set<int64_t> LocalTextIndex::candidateFiles(const std::vector<std::string>& queryTerms) const
{
    std::set<int64_t> out;

    for (size_t i = 0; i < queryTerms.size(); ++i) {
        const std::map<std::string, std::map<int64_t, size_t> >::const_iterator post =
            m_postings.find(queryTerms[i]);
        if (post == m_postings.end())
            continue;
        for (std::map<int64_t, size_t>::const_iterator f = post->second.begin();
             f != post->second.end(); ++f)
            out.insert(f->first);           // set 天然去重 (同一文档被多个查询词命中只出现一次)
    }

    return out;
}

size_t LocalTextIndex::docCount() const
{
    return m_docs.size();
}

size_t LocalTextIndex::docFreq(const std::string& term) const
{
    const std::map<std::string, std::map<int64_t, size_t> >::const_iterator post = m_postings.find(term);
    return (post == m_postings.end()) ? 0 : post->second.size();
}

std::map<std::string, size_t> LocalTextIndex::documentTerms(int64_t fileId) const
{
    const std::map<int64_t, std::map<std::string, size_t> >::const_iterator docIt = m_docTerms.find(fileId);
    return (docIt == m_docTerms.end()) ? std::map<std::string, size_t>() : docIt->second;
}

void LocalTextIndex::clear()
{
    m_postings.clear();
    m_docTerms.clear();
    m_docs.clear();
}
