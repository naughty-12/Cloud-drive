#include "UploadCache.h"
#include <cstring>
#include <cstdio>

UploadCache::UploadCache()
    : m_db(nullptr)
    , m_ok(false)
{
}

UploadCache::~UploadCache()
{
    close();
}

bool UploadCache::open(const char* dbPath)
{
    int rc = sqlite3_open(dbPath, &m_db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "UploadCache: cannot open database: %s\n", sqlite3_errmsg(m_db));
        m_ok = false;
        return false;
    }
    initSchema();
    m_ok = true;
    return true;
}

void UploadCache::close()
{
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
    m_ok = false;
}

void UploadCache::initSchema()
{
    const char* sql =
        "CREATE TABLE IF NOT EXISTS upload_cache("
        "  file_path TEXT PRIMARY KEY,"
        "  sha256 TEXT NOT NULL,"
        "  mtime INTEGER NOT NULL,"
        "  size INTEGER NOT NULL,"
        "  cached_at INTEGER DEFAULT (strftime('%s','now'))"
        ");";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "UploadCache: SQL error (initSchema): %s\n", errMsg);
        sqlite3_free(errMsg);
        m_ok = false;
    }
}

std::string UploadCache::lookup(const std::string& filePath, int64_t mtime, int64_t size)
{
    if (!m_ok || !m_db) return "";

    const char* sql = "SELECT sha256 FROM upload_cache WHERE file_path=? AND mtime=? AND size=?";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "UploadCache: prepare failed (lookup): %s\n", sqlite3_errmsg(m_db));
        return "";
    }

    sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, mtime);
    sqlite3_bind_int64(stmt, 3, size);

    std::string result;
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const char* sha = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (sha) {
            result = sha;
        }
    }

    sqlite3_finalize(stmt);
    return result;
}

void UploadCache::store(const std::string& filePath, int64_t mtime, int64_t size,
                        const std::string& sha256)
{
    if (!m_ok || !m_db) return;

    const char* sql = "INSERT OR REPLACE INTO upload_cache(file_path, sha256, mtime, size) VALUES(?,?,?,?)";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "UploadCache: prepare failed (store): %s\n", sqlite3_errmsg(m_db));
        return;
    }

    sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sha256.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, mtime);
    sqlite3_bind_int64(stmt, 4, size);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "UploadCache: step failed (store): %s\n", sqlite3_errmsg(m_db));
    }

    sqlite3_finalize(stmt);
}

void UploadCache::purge(const std::string& filePath)
{
    if (!m_ok || !m_db) return;

    const char* sql = "DELETE FROM upload_cache WHERE file_path=?";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "UploadCache: prepare failed (purge): %s\n", sqlite3_errmsg(m_db));
        return;
    }

    sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "UploadCache: step failed (purge): %s\n", sqlite3_errmsg(m_db));
    }

    sqlite3_finalize(stmt);
}
