#ifndef LOCALTEXTINDEX_H
#define LOCALTEXTINDEX_H

/**
 * @file LocalTextIndex.h
 * @brief 内存倒排索引 —— 本地检索引擎的召回层 (零依赖、无网络、纯计算)。
 *
 * 结构 (见 .dsh-dev/specs/2026-09-07-local-engine-first-design.md 4.2):
 *   - 倒排索引 m_postings : 词元 → (fileId → 该词在该文档内的计数)   ← candidateFiles 召回
 *   - 前向索引 m_docTerms : fileId → (词元 → 计数)                  ← TF-IDF 文档向量与文档范数
 *   - 文档集合 m_docs     : 已索引 fileId 集合                      ← docCount (IDF 的 N)
 *
 * 为什么同时维护前向索引: TF-IDF 余弦相似度的分母需要「文档向量的完整范数」,
 * 只靠倒排表只能拿到查询词那几维 → 不是真正的余弦。前向索引让在线检索仍为
 * O(查询词数 × 命中文档数) 的倒排访问 + 单文档范数一遍扫描, 内存增量与倒排表同量级。
 *
 * 单文件内容由调用方 (server: FileStorage 读块 + ITextExtractor 提取) 提供;
 * 本模块只做词元化与计数, 不触碰磁盘/数据库/网络。
 *
 * 线程安全: 非线程安全 —— 调用方需自行串行化 (当前由 DbWorker 线程单线程访问)。
 */

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

class LocalTextIndex
{
public:
    /// 索引一个文件的内容 (内部 tokenize + 词频统计)。
    /// 重复添加同一 fileId 会先移除旧项再重建 (重新索引/覆盖语义)。
    /// 空内容亦登记文档 (docCount 计入), 但不产生任何倒排项。
    void addDocument(int64_t fileId, const std::string& utf8Text);

    /// 移除一个文件的全部索引项 (删除文件 / 秒传回收 / 重新索引前)。幂等, 不存在时无操作。
    void removeDocument(int64_t fileId);

    /// 某词元在某文档内的词频 (计数)。计划接口签名: 返回 {term: 计数}。
    /// 该词不在文档内 (或文档未索引) 时返回空 map, 调用方无需特判。
    std::map<std::string, double> termFreq(int64_t fileId, const std::string& term) const;

    /// 倒排召回: 返回含任意一个查询词元的文档集合 (并集去重)。
    /// 不做排序 (排序是 LocalSearchEngine 的职责)。
    std::set<int64_t> candidateFiles(const std::vector<std::string>& queryTerms) const;

    /// 已索引文档总数 (TF-IDF 的 N)。含无词元的空文档。
    size_t docCount() const;

    /// 含该词元的文档数 (TF-IDF 的 df)。词元不在索引中时返回 0。
    size_t docFreq(const std::string& term) const;

    /// 某文档的 词元 → 计数 (前向索引, 返回副本)。文档未索引或无词元时返回空 map。
    std::map<std::string, size_t> documentTerms(int64_t fileId) const;

    /// 清空全部索引 (启动重建 / 测试用)。
    void clear();

private:
    /// 倒排表: 词元 → (fileId → 词频)。空 postings 列表在删除时同步擦除, 不留空壳。
    std::map<std::string, std::map<int64_t, size_t> > m_postings;
    /// 前向表: fileId → (词元 → 词频)。
    std::map<int64_t, std::map<std::string, size_t> > m_docTerms;
    /// 已索引文档集合。
    std::set<int64_t> m_docs;
};

#endif // LOCALTEXTINDEX_H
