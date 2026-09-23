#ifndef FILESTORAGE_H
#define FILESTORAGE_H

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <fstream>

struct BlockInfo {
    int64_t fileId;
    int     blockSeq;
    int64_t offset;     // 在 blocks.dat 中的偏移
    int     length;     // 实际数据长度
};

class FileStorage {
public:
    FileStorage();
    ~FileStorage();

    // 初始化：打开/创建 blocks.dat + blocks.idx，basePath 形如 "D:\\disk1\\"
    bool init(const std::string& basePath);

    // 写入一个块——追加到 blocks.dat，并在索引中记录
    // 返回数据写入处的偏移
    int64_t writeBlock(int64_t fileId, int blockSeq, const char* data, int len);

    // 按 file_id + block_seq 读取一个块
    // 未找到时返回空字符串
    std::string readBlock(int64_t fileId, int blockSeq);

    // 获取某个文件的全部块（按 block_seq 排序）
    std::vector<BlockInfo> getFileBlocks(int64_t fileId);

    // 由全部块计算文件总大小（各块长度之和）
    int64_t getFileSize(int64_t fileId);

    // 从已存储的块中读取任意字节区间
    // offset：逻辑（重组后）文件中的字节偏移
    // length：要读取的字节数
    // 返回请求的字节区间，出错时返回空字符串
    std::string readRange(int64_t fileId, int64_t offset, int length);

    // 删除某文件的全部块（将索引条目标记为已删除）
    void deleteFile(int64_t fileId);

    // 为上传创建临时文件
    std::string createTempFile(int64_t fileId);

    // 提交临时文件 → 追加到 blocks.dat 与索引
    std::vector<BlockInfo> commitTempFile(int64_t fileId, const std::string& tempPath);

    // 获取存储统计信息
    int64_t totalBlocksSize() const { return m_totalSize; }

private:
    std::string m_basePath;
    std::string m_blocksPath;    // blocks.dat
    std::string m_indexPath;      // blocks.idx

    std::mutex m_mutex;
    std::ofstream m_blocksStream;
    int64_t m_totalSize;

    // 索引文件格式（为简单起见使用文本）：
    // 每行：file_id,block_seq,offset,length
    void writeIndexEntry(int64_t fileId, int blockSeq, int64_t offset, int length);
    std::vector<BlockInfo> readAllIndex();
};

#endif
