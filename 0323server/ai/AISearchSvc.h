#ifndef AISEARCHSVC_H
#define AISEARCHSVC_H

#include "AIServiceBase.h"
#include "Packdef.h"
#include <string>
#include <vector>
#include <functional>

// Forward declare to avoid pulling in MySQL headers
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

    // Constructor: takes MySQL handle for embedding storage
    explicit AISearchSvc(MySqlWrapper* sql = nullptr);
    void setMySql(MySqlWrapper* sql) { m_sql = sql; }

    // Synchronous search (for DB worker thread)
    STRU_AISEARCHRS search(const std::string& query, int64_t userId);

    // Async search (does embedding API call in background)
    void searchAsync(const std::string& query, int64_t userId, SearchCallback cb);

    // Called after upload: compute and store embedding for a file
    void indexFile(int64_t fileId, const std::string& content);

    // Cosine similarity between two float vectors
    static double cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b);

private:
    MySqlWrapper* m_sql;  // for loading/storing embeddings (may be null → stub)

    std::vector<float> loadEmbedding(int64_t fileId);
    void storeEmbedding(int64_t fileId, const std::vector<float>& embedding);

    // Serialization helpers: float vector ↔ CSV string
    static std::string serializeEmbedding(const std::vector<float>& emb);
    static std::vector<float> deserializeEmbedding(const std::string& csv);
};
#endif
