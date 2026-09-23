#ifndef AISEARCHSVC_H
#define AISEARCHSVC_H

#include "AIServiceBase.h"
#include "Packdef.h"
#include <string>
#include <vector>
#include <functional>

// 前置声明，避免引入 MySQL 头文件
class MySqlWrapper;

struct SearchResultItem {
    int64_t     fileId;
    std::string fileName;
    int64_t     fileSize;
    double      similarity;
    std::string matchReason;
    std::string fileSHA256;
};

class AISearchSvc : public AIServiceBase {
public:
    using SearchCallback = std::function<void(const STRU_AISEARCHRS&)>;

    // 构造函数: 接收 MySQL 句柄用于 Embedding 存取
    explicit AISearchSvc(MySqlWrapper* sql = nullptr);
    void setMySql(MySqlWrapper* sql) { m_sql = sql; }

    // 同步搜索（供 DB Worker 线程使用）
    STRU_AISEARCHRS search(const std::string& query, int64_t userId);

    // 异步搜索（在后台调用 Embedding API）
    void searchAsync(const std::string& query, int64_t userId, SearchCallback cb);

    // 上传后调用: 计算并存储文件的 Embedding
    void indexFile(int64_t fileId, const std::string& content);

    // 两个 float 向量之间的余弦相似度
    static double cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b);

private:
    MySqlWrapper* m_sql;  // 用于加载/存储 Embedding（可为空 → 桩实现）

    std::vector<float> loadEmbedding(int64_t fileId);
    void storeEmbedding(int64_t fileId, const std::vector<float>& embedding);

    // 序列化辅助: float 向量 ↔ CSV 字符串
    static std::string serializeEmbedding(const std::vector<float>& emb);
    static std::vector<float> deserializeEmbedding(const std::string& csv);
};
#endif
