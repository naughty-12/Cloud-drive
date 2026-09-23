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
    int64_t  lastOffset;     // 已接收的字节数（== 续传位置）
    std::string tempPath;
    std::string fileHash;    // SHA-256 指纹
    std::string state;       // 状态："uploading" | "verifying" | "committed"
};

class SqliteState {
public:
    SqliteState();
    ~SqliteState();

    bool open(const char* dbPath);  // 例如 "D:\\disk1\\upload_state.db"
    void close();

    // CRUD 操作
    bool createState(const UploadState& s);
    UploadState* getState(const std::string& fileHash, int64_t userId);
    bool updateProgress(const std::string& fileHash, int64_t userId,
                        int completedBlocks, int64_t lastOffset);
    bool setState(const std::string& fileHash, int64_t userId, const std::string& state);

    // 恢复：服务器重启后获取所有未完成的上传
    std::vector<UploadState> getUnfinishedUploads();

    // 清理
    bool deleteState(const std::string& fileHash, int64_t userId);

private:
    sqlite3* m_db;
    std::mutex m_mutex;  // F6-1 修复：保护 SQLite 句柄免受并发 IOCP 线程访问
    void initSchema();
};

#endif
