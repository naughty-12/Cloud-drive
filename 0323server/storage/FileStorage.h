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
    int64_t offset;     // offset in blocks.dat
    int     length;     // actual data length
};

class FileStorage {
public:
    FileStorage();
    ~FileStorage();

    // Initialize: open/create blocks.dat + blocks.idx. basePath = "D:\\disk1\\"
    bool init(const std::string& basePath);

    // Write a block — appends to blocks.dat, records in index
    // Returns the offset where data was written
    int64_t writeBlock(int64_t fileId, int blockSeq, const char* data, int len);

    // Read a block by file_id + block_seq
    // Returns empty string if not found
    std::string readBlock(int64_t fileId, int blockSeq);

    // Get all blocks for a file (sorted by block_seq)
    std::vector<BlockInfo> getFileBlocks(int64_t fileId);

    // Get total file size from all blocks (sum of block lengths)
    int64_t getFileSize(int64_t fileId);

    // Read arbitrary byte range from stored blocks
    // offset: byte offset in the logical (reassembled) file
    // length: number of bytes to read
    // Returns the requested byte range, or empty string on error
    std::string readRange(int64_t fileId, int64_t offset, int length);

    // Delete all blocks for a file (marks index entries as deleted)
    void deleteFile(int64_t fileId);

    // Create temp file for upload
    std::string createTempFile(int64_t fileId);

    // Commit temp file → append to blocks.dat + index
    std::vector<BlockInfo> commitTempFile(int64_t fileId, const std::string& tempPath);

    // Get storage stats
    int64_t totalBlocksSize() const { return m_totalSize; }

private:
    std::string m_basePath;
    std::string m_blocksPath;    // blocks.dat
    std::string m_indexPath;      // blocks.idx

    std::mutex m_mutex;
    std::ofstream m_blocksStream;
    int64_t m_totalSize;

    // Index file format (text-based for simplicity):
    // Each line: file_id,block_seq,offset,length
    void writeIndexEntry(int64_t fileId, int blockSeq, int64_t offset, int length);
    std::vector<BlockInfo> readAllIndex();
};

#endif
