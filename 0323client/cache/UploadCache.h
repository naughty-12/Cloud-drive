#ifndef UPLOADCACHE_H
#define UPLOADCACHE_H

#include <string>
#include <cstdint>
#include <sqlite3.h>

class UploadCache {
public:
    UploadCache();
    ~UploadCache();

    bool open(const char* dbPath);  // 例如 "./upload_cache.db"
    void close();

    // 按文件路径 + mtime + size 查找 -> SHA-256（若文件未变）
    // 未找到时返回空字符串
    std::string lookup(const std::string& filePath, int64_t mtime, int64_t size);

    // 存储/更新缓存条目
    void store(const std::string& filePath, int64_t mtime, int64_t size,
               const std::string& sha256);

    // 清除过期条目（文件已不存在）
    void purge(const std::string& filePath);

private:
    sqlite3* m_db;
    bool m_ok;
    void initSchema();
};

#endif
