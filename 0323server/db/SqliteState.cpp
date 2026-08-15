#include "SqliteState.h"
#include <cstring>

SqliteState::SqliteState() : m_db(nullptr) {}

SqliteState::~SqliteState() { close(); }

bool SqliteState::open(const char* dbPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (sqlite3_open(dbPath, &m_db) != SQLITE_OK) return false;
    initSchema();
    return true;
}

void SqliteState::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db) { sqlite3_close(m_db); m_db = nullptr; }
}

void SqliteState::initSchema() {
    // F6-1: called only internally under lock, no separate lock needed
    const char* sql =
        "CREATE TABLE IF NOT EXISTS upload_state("
        "file_id       BIGINT PRIMARY KEY,"
        "user_id       BIGINT NOT NULL,"
        "file_size     BIGINT NOT NULL,"
        "total_blocks  INT NOT NULL,"
        "completed_blocks INT DEFAULT 0,"
        "last_offset   BIGINT DEFAULT 0,"
        "temp_path     TEXT NOT NULL,"
        "file_hash     TEXT NOT NULL,"
        "state         TEXT DEFAULT 'uploading',"
        "created_at    DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "updated_at    DATETIME DEFAULT CURRENT_TIMESTAMP)";
    sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);
}

bool SqliteState::createState(const UploadState& s) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "INSERT OR REPLACE INTO upload_state"
        "(file_id,user_id,file_size,total_blocks,completed_blocks,last_offset,temp_path,file_hash,state)"
        " VALUES(?,?,?,?,?,?,?,?,?)";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, s.fileId);
    sqlite3_bind_int64(stmt, 2, s.userId);
    sqlite3_bind_int64(stmt, 3, s.fileSize);
    sqlite3_bind_int( stmt, 4, s.totalBlocks);
    sqlite3_bind_int( stmt, 5, s.completedBlocks);
    sqlite3_bind_int64(stmt, 6, s.lastOffset);
    sqlite3_bind_text(stmt, 7, s.tempPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, s.fileHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, s.state.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

UploadState* SqliteState::getState(const std::string& fileHash, int64_t userId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "SELECT file_id,user_id,file_size,total_blocks,completed_blocks,last_offset,temp_path,file_hash,state"
        " FROM upload_state WHERE file_hash=? AND user_id=?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, fileHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, userId);

    UploadState* result = nullptr;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = new UploadState();
        result->fileId          = sqlite3_column_int64(stmt, 0);
        result->userId          = sqlite3_column_int64(stmt, 1);
        result->fileSize        = sqlite3_column_int64(stmt, 2);
        result->totalBlocks     = sqlite3_column_int(stmt, 3);
        result->completedBlocks = sqlite3_column_int(stmt, 4);
        result->lastOffset      = sqlite3_column_int64(stmt, 5);
        const char* tp = (const char*)sqlite3_column_text(stmt, 6);
        if (tp) result->tempPath = tp;
        const char* fh = (const char*)sqlite3_column_text(stmt, 7);
        if (fh) result->fileHash = fh;
        const char* st = (const char*)sqlite3_column_text(stmt, 8);
        if (st) result->state = st;
    }
    sqlite3_finalize(stmt);
    return result;
}

bool SqliteState::updateProgress(const std::string& fileHash, int64_t userId,
                                  int completedBlocks, int64_t lastOffset) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "UPDATE upload_state SET completed_blocks=?, last_offset=?,"
        " updated_at=CURRENT_TIMESTAMP WHERE file_hash=? AND user_id=?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, completedBlocks);
    sqlite3_bind_int64(stmt, 2, lastOffset);
    sqlite3_bind_text(stmt, 3, fileHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, userId);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool SqliteState::setState(const std::string& fileHash, int64_t userId,
                            const std::string& state) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql =
        "UPDATE upload_state SET state=?, updated_at=CURRENT_TIMESTAMP WHERE file_hash=? AND user_id=?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, state.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fileHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, userId);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<UploadState> SqliteState::getUnfinishedUploads() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<UploadState> results;
    const char* sql =
        "SELECT file_id,user_id,file_size,total_blocks,completed_blocks,last_offset,temp_path,file_hash,state"
        " FROM upload_state WHERE state='uploading'";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        UploadState s;
        s.fileId          = sqlite3_column_int64(stmt, 0);
        s.userId          = sqlite3_column_int64(stmt, 1);
        s.fileSize        = sqlite3_column_int64(stmt, 2);
        s.totalBlocks     = sqlite3_column_int(stmt, 3);
        s.completedBlocks = sqlite3_column_int(stmt, 4);
        s.lastOffset      = sqlite3_column_int64(stmt, 5);
        const char* tp = (const char*)sqlite3_column_text(stmt, 6);
        if (tp) s.tempPath = tp;
        const char* fh = (const char*)sqlite3_column_text(stmt, 7);
        if (fh) s.fileHash = fh;
        const char* st = (const char*)sqlite3_column_text(stmt, 8);
        if (st) s.state = st;
        results.push_back(s);
    }
    sqlite3_finalize(stmt);
    return results;
}

bool SqliteState::deleteState(const std::string& fileHash, int64_t userId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const char* sql = "DELETE FROM upload_state WHERE file_hash=? AND user_id=?";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, fileHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, userId);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}
