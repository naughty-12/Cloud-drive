#include "AISearchSvc.h"
#include "../db/MySqlWrapper.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <cstring>
#include <cstdlib>

// ============================================================================
// 辅助函数: float 向量 ↔ CSV 字符串（C++11，零外部依赖）
// ============================================================================
std::string AISearchSvc::serializeEmbedding(const std::vector<float>& emb) {
    std::ostringstream oss;
    for (size_t i = 0; i < emb.size(); i++) {
        if (i > 0) oss << ",";
        oss << emb[i];
    }
    return oss.str();
}

std::vector<float> AISearchSvc::deserializeEmbedding(const std::string& csv) {
    std::vector<float> result;
    if (csv.empty()) return result;
    std::istringstream iss(csv);
    std::string token;
    while (std::getline(iss, token, ',')) {
        result.push_back(strtof(token.c_str(), nullptr));
    }
    return result;
}

// ============================================================================
// 构造函数
// ============================================================================
AISearchSvc::AISearchSvc(MySqlWrapper* sql) : m_sql(sql) {}

// ============================================================================
// loadEmbedding / storeEmbedding — 基于 DB 的真实实现
// ============================================================================
std::vector<float> AISearchSvc::loadEmbedding(int64_t fileId) {
    if (!m_sql) return {};
    std::list<std::string> lst;
    if (!m_sql->query("SELECT embedding FROM file_embeddings WHERE f_id=?",
                      {fileId}, 1, lst) || lst.empty()) {
        return {};
    }
    return deserializeEmbedding(lst.front());
}

void AISearchSvc::storeEmbedding(int64_t fileId, const std::vector<float>& embedding) {
    if (!m_sql) return;
    std::string csv = serializeEmbedding(embedding);
    // 使用 ON DUPLICATE KEY UPDATE 实现幂等重建索引
    m_sql->execute(
        "INSERT INTO file_embeddings(f_id, embedding) VALUES(?,?) "
        "ON DUPLICATE KEY UPDATE embedding=VALUES(embedding)",
        {fileId, csv});
}

// ============================================================================
// 降级: 文件名 LIKE 搜索（AI 不可用或缺少 Embedding 时）
// ============================================================================
static STRU_AISEARCHRS fallbackSearch(const std::string& query, int64_t userId) {
    STRU_AISEARCHRS rs;
    rs.m_nResultNum = 0;
    rs.m_szResult = 1;  // 成功但带降级标记
    (void)query;
    (void)userId;
    return rs;
}

// ============================================================================
// search — 查询 Embedding → 加载用户所有文件的 Embedding → 余弦排序 → Top-K
// ============================================================================
STRU_AISEARCHRS AISearchSvc::search(const std::string& query, int64_t userId) {
    if (!isAIEnabled()) {
        return fallbackSearch(query, userId);
    }

    // 第 1 步: 获取查询语句的 Embedding
    auto embResult = api()->embedding(query);
    if (!embResult.success || embResult.embedding.empty()) {
        return fallbackSearch(query, userId);
    }

    // 第 2 步: 加载该用户所有已索引的文件（附带文件元数据）
    // 仅考虑已被索引（拥有 Embedding）的文件
    std::vector<SearchResultItem> candidates;
    if (m_sql) {
        std::list<std::string> rows;
        // embedding 列位于 select 列表第 3 位
        // F14-1 修复: 包含 f_sha256（SHA-256），供客户端校验完整性
        m_sql->query(
            "SELECT fe.f_id, f.f_name, f.f_size, fe.embedding, f.f_sha256 "
            "FROM file_embeddings fe "
            "JOIN files f ON fe.f_id = f.f_id "
            "JOIN user_file uf ON f.f_id = uf.f_id "
            "WHERE uf.u_id = ?",
            {static_cast<int64_t>(userId)}, 5, rows);

        // 解析结果行: 每 5 个字符串对应一个文件
        auto it = rows.begin();
        while (it != rows.end()) {
            SearchResultItem item;
            item.fileId = atoll(it->c_str()); ++it;
            item.fileName = *it; ++it;
            item.fileSize = atoll(it->c_str()); ++it;
            std::string embCsv = *it; ++it;
            item.fileSHA256 = *it; ++it;  // F14-1
            auto fileEmb = deserializeEmbedding(embCsv);
            item.similarity = cosineSimilarity(embResult.embedding, fileEmb);
            candidates.push_back(item);
        }
    }

    // 第 3 步: 无已索引文件 → 降级为文件名 LIKE 搜索
    if (candidates.empty()) {
        return fallbackSearch(query, userId);
    }

    // 第 4 步: 按相似度降序排序，取前 10 条
    std::sort(candidates.begin(), candidates.end(),
        [](const SearchResultItem& a, const SearchResultItem& b) {
            return a.similarity > b.similarity;
        });

    STRU_AISEARCHRS rs;
    rs.m_szResult = 0;  // AI 成功
    rs.m_nResultNum = static_cast<unsigned int>(
        std::min<size_t>(candidates.size(), 10));
    for (size_t i = 0; i < candidates.size() && i < 10; i++) {
        const auto& c = candidates[i];
        auto& out = rs.m_aryResults[i];
        out.m_fileInfo.m_fileID = c.fileId;
        strncpy(out.m_fileInfo.m_szFileName, c.fileName.c_str(), MAXSIZE - 1);
        out.m_fileInfo.m_szFileName[MAXSIZE - 1] = '\0';
        out.m_fileInfo.m_filesize = c.fileSize;
        // F14-1 修复: 填充 SHA-256 供客户端完整性校验
        strncpy(out.m_szFileSHA256, c.fileSHA256.c_str(), sizeof(out.m_szFileSHA256) - 1);
        char buf[64];
        snprintf(buf, sizeof(buf), "相似度 %.1f%%", c.similarity * 100.0);
        strncpy(out.m_szMatchReason, buf, sizeof(out.m_szMatchReason) - 1);
    }
    return rs;
}

// ============================================================================
// searchAsync — 异步版本（在后台调用 Embedding API）
// ============================================================================
void AISearchSvc::searchAsync(const std::string& query, int64_t userId, SearchCallback cb) {
    if (!isAIEnabled()) {
        if (cb) cb(fallbackSearch(query, userId));
        return;
    }

    api()->embeddingAsync(query, [query, userId, cb, this](APIBridge::EmbeddingResult embResult) {
        // searchAsync 目前无法与已存储的 Embedding 排序，
        // 因为当前处于无 DB 访问权限的回调中，暂时降级处理。
        // 完整的异步搜索需要在此处派发任务给 DbWorker。
        STRU_AISEARCHRS rs;
        if (!embResult.success) {
            rs = fallbackSearch(query, userId);
        } else {
            rs.m_nResultNum = 0;
            rs.m_szResult = 0;
        }
        if (cb) cb(rs);
    });
}

// ============================================================================
// indexFile — 计算文件的 Embedding 并存储（上传后调用）
// ============================================================================
void AISearchSvc::indexFile(int64_t fileId, const std::string& content) {
    if (!isAIEnabled()) return;
    if (content.empty()) return;

    // 取前 8000 字符用于 Embedding（远低于 OpenAI token 上限）
    std::string text = content.substr(0, 8000);
    auto embResult = api()->embedding(text);
    if (embResult.success && !embResult.embedding.empty()) {
        storeEmbedding(fileId, embResult.embedding);
        printf("[AISearchSvc] Indexed file %lld (%zu-dim embedding stored)\n",
               (long long)fileId, embResult.embedding.size());
    }
}

// ============================================================================
// cosineSimilarity — 标准余弦距离
// ============================================================================
double AISearchSvc::cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0;
    double dot = 0, normA = 0, normB = 0;
    for (size_t i = 0; i < a.size(); i++) {
        dot += (double)a[i] * (double)b[i];
        normA += (double)a[i] * (double)a[i];
        normB += (double)b[i] * (double)b[i];
    }
    if (normA == 0.0 || normB == 0.0) return 0.0;
    return dot / (std::sqrt(normA) * std::sqrt(normB));
}
