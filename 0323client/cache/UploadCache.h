#ifndef UPLOADCACHE_H
#define UPLOADCACHE_H

#include <string>
#include <cstdint>
#include <sqlite3.h>

class UploadCache {
public:
    UploadCache();
    ~UploadCache();

    bool open(const char* dbPath);  // e.g. "./upload_cache.db"
    void close();

    // Lookup by file path + mtime + size -> SHA-256 (if unchanged)
    // Returns empty string if not found
    std::string lookup(const std::string& filePath, int64_t mtime, int64_t size);

    // Store/update cache entry
    void store(const std::string& filePath, int64_t mtime, int64_t size,
               const std::string& sha256);

    // Remove stale entries (file no longer exists)
    void purge(const std::string& filePath);

private:
    sqlite3* m_db;
    bool m_ok;
    void initSchema();
};

#endif
