#ifndef SQLITESTATE_H
#define SQLITESTATE_H

#include <string>
#include <cstdint>
#include <vector>
#include <mutex>
#include <sqlite3.h>

struct UploadState {
    int64_t  fileId;
    int64_t  userId;
    int64_t  fileSize;
    int      totalBlocks;
    int      completedBlocks;
    int64_t  lastOffset;     // bytes received so far (== resume position)
    std::string tempPath;
    std::string fileHash;    // SHA-256 fingerprint
    std::string state;       // "uploading" | "verifying" | "committed"
};

class SqliteState {
public:
    SqliteState();
    ~SqliteState();

    bool open(const char* dbPath);  // e.g. "D:\\disk1\\upload_state.db"
    void close();

    // CRUD operations
    bool createState(const UploadState& s);
    UploadState* getState(const std::string& fileHash, int64_t userId);
    bool updateProgress(const std::string& fileHash, int64_t userId,
                        int completedBlocks, int64_t lastOffset);
    bool setState(const std::string& fileHash, int64_t userId, const std::string& state);

    // Recovery: get all unfinished uploads after server restart
    std::vector<UploadState> getUnfinishedUploads();

    // Cleanup
    bool deleteState(const std::string& fileHash, int64_t userId);

private:
    sqlite3* m_db;
    std::mutex m_mutex;  // F6-1 fix: protect SQLite handle from concurrent IOCP threads
    void initSchema();
};

#endif
