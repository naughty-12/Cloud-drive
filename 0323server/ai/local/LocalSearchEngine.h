#ifndef LOCALSEARCHENGINE_H
#define LOCALSEARCHENGINE_H

/**
 * @file LocalSearchEngine.h
 * @brief 本地检索排序层 —— 倒排召回 (LocalTextIndex) + TF-IDF 加权余弦相似度。
 *
 * 算法 (见 .dsh-dev/specs/2026-09-07-local-engine-first-design.md 4.2):
 *   1. IDF(t)  = log((N + 1) / (df(t) + 1)) + 1        // N = docCount, df = 含 t 的文档数
 *      平滑写法保证 IDF 恒 ≥ 1 > 0: 词再常见也不会权重归零或变负 (避免整条评分塌陷)。
 *   2. 文档向量 d[f][t] = tf(t, f) * IDF(t)             // tf = 该词在该文档内的计数
 *      查询向量 q[t]    = 查询串中该词的频次 (不做 IDF 加权, 见设计 §4.2)
 *   3. score(f) = (q · d[f]) / (|q| * |d[f]|)           // 余弦相似度, 取值 (0, 1]
 *
 * 为什么用余弦而不是"命中词个数"或"词频求和": 余弦对文档长度与词频量纲不敏感,
 * 「两个查询词都命中但文档较长」也能量化出高于「只命中一个词」的文档 (见单测
 * moreMatchedTermsRankHigher / cosineMatchesDocumentedFormula)。
 *
 * 为什么文档范数取**全部词元**而不是只取查询命中的那几维: 余弦的定义就是在整个
 * 词元空间上求夹角; 只统计查询维度会退化成"查询词覆盖率", 短文档会被系统性高估。
 *
 * 排序: 分数降序; 分数相同按 fileId 升序 (结果确定可复现)。
 *
 * 线程安全: 只读索引, search() 为 const; 索引本身的并发写由调用方串行化。
 */

#include <cstdint>
#include <vector>
#include <string>

class LocalTextIndex;

class LocalSearchEngine
{
public:
    /// 检索结果项 (fileId 供上层映射回文件元数据)。
    struct Hit {
        int64_t fileId;
        double  score;      ///< 余弦相似度, (0, 1]
    };

    /// 索引不归本类所有, 生命周期由调用方保证 (传 0 时 search 安全返回空)。
    explicit LocalSearchEngine(LocalTextIndex* index);

    /// 检索: 词元化查询串 → 倒排召回 → TF-IDF 余弦排序 → 取前 topN 条。
    /// 无命中 / 空查询 / 空索引 / index 为空指针 → 返回空 vector;
    /// topN = 0 表示不返回任何结果。
    std::vector<Hit> search(const std::string& query, size_t topN) const;

private:
    /// 平滑 IDF: log((docCount + 1) / (docFreq + 1)) + 1。
    static double idf(size_t docCount, size_t docFreq);

    LocalTextIndex* m_index;    ///< 非拥有 (non-owning) 指针
};

#endif // LOCALSEARCHENGINE_H
