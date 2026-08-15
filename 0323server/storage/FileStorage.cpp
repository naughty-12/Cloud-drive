#include "FileStorage.h"
#include <windows.h>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <sys/stat.h>

FileStorage::FileStorage() : m_totalSize(0) {}

FileStorage::~FileStorage() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_blocksStream.is_open()) {
        m_blocksStream.close();
    }
}

bool FileStorage::init(const std::string& basePath) {
    m_basePath = basePath;
    // Ensure trailing backslash
    if (!m_basePath.empty() && m_basePath.back() != '\\') {
        m_basePath += '\\';
    }

    m_blocksPath = m_basePath + "blocks.dat";
    m_indexPath  = m_basePath + "blocks.idx";

    // Create base directory (CreateDirectoryA is idempotent if dir exists)
    CreateDirectoryA(m_basePath.c_str(), NULL);

    // Create temp directory
    std::string tempDir = m_basePath + "temp\\";
    CreateDirectoryA(tempDir.c_str(), NULL);

    // Open blocks.dat in append+write mode.
    // If the file doesn't exist, first create it.
    struct stat st;
    bool exists = (stat(m_blocksPath.c_str(), &st) == 0);
    if (!exists) {
        // Create the file
        std::ofstream create(m_blocksPath, std::ios::binary);
        create.close();
    }

    m_blocksStream.open(m_blocksPath, std::ios::app | std::ios::binary);
    if (!m_blocksStream.is_open()) {
        return false;
    }

    // Load existing index and compute total size
    auto blocks = readAllIndex();
    m_totalSize = 0;
    for (const auto& b : blocks) {
        int64_t end = b.offset + b.length;
        if (end > m_totalSize) m_totalSize = end;
    }

    return true;
}

void FileStorage::writeIndexEntry(int64_t fileId, int blockSeq, int64_t offset, int length) {
    // Open index file in append mode to add one line
    std::ofstream idx(m_indexPath, std::ios::app);
    if (idx.is_open()) {
        idx << fileId << "," << blockSeq << "," << offset << "," << length << "\n";
        idx.close();
    }
}

std::vector<BlockInfo> FileStorage::readAllIndex() {
    std::vector<BlockInfo> result;
    std::ifstream idx(m_indexPath);
    if (!idx.is_open()) return result;

    std::string line;
    while (std::getline(idx, line)) {
        // Skip empty lines and deleted entries (prefixed with #)
        if (line.empty() || line[0] == '#') continue;

        BlockInfo info;
        char comma;
        std::istringstream iss(line);
        iss >> info.fileId >> comma >> info.blockSeq >> comma >> info.offset >> comma >> info.length;
        if (!iss.fail()) {
            result.push_back(info);
        }
    }
    return result;
}

int64_t FileStorage::writeBlock(int64_t fileId, int blockSeq, const char* data, int len) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (len <= 0) return -1;

    // F16-4 fix: idempotency — if this (fileId, blockSeq) already written, return existing offset
    {
        auto blocks = readAllIndex();
        for (const auto& b : blocks) {
            if (b.fileId == fileId && b.blockSeq == blockSeq) {
                return b.offset;  // already exists — no duplicate write
            }
        }
    }

    // Seek to end of blocks.dat to get current write offset.
    // In append mode, seekp(0, end) + tellp() gives the end position before writing.
    m_blocksStream.seekp(0, std::ios::end);
    int64_t offset = m_blocksStream.tellp();

    // Write data
    m_blocksStream.write(data, len);
    m_blocksStream.flush();

    // Record index entry
    writeIndexEntry(fileId, blockSeq, offset, len);

    // Update total size
    m_totalSize = offset + len;

    return offset;
}

std::string FileStorage::readBlock(int64_t fileId, int blockSeq) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto blocks = readAllIndex();
    for (const auto& b : blocks) {
        if (b.fileId == fileId && b.blockSeq == blockSeq) {
            // Open a separate read-only stream for the data
            std::ifstream blocksFile(m_blocksPath, std::ios::binary);
            if (!blocksFile.is_open()) return "";

            blocksFile.seekg(b.offset, std::ios::beg);
            if (!blocksFile.good()) return "";

            std::string data(b.length, '\0');
            blocksFile.read(&data[0], b.length);
            return data;
        }
    }
    return "";
}

std::vector<BlockInfo> FileStorage::getFileBlocks(int64_t fileId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto blocks = readAllIndex();
    std::vector<BlockInfo> result;
    for (const auto& b : blocks) {
        if (b.fileId == fileId) {
            result.push_back(b);
        }
    }
    // Sort by block sequence number
    std::sort(result.begin(), result.end(),
        [](const BlockInfo& a, const BlockInfo& b) {
            return a.blockSeq < b.blockSeq;
        });
    return result;
}

int64_t FileStorage::getFileSize(int64_t fileId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto blocks = readAllIndex();
    int64_t total = 0;
    for (const auto& b : blocks) {
        if (b.fileId == fileId) {
            total += b.length;
        }
    }
    return total;
}

std::string FileStorage::readRange(int64_t fileId, int64_t offset, int length) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (length <= 0 || offset < 0) return "";

    // Get sorted blocks for this file
    auto allBlocks = readAllIndex();
    std::vector<BlockInfo> blocks;
    for (const auto& b : allBlocks) {
        if (b.fileId == fileId) {
            blocks.push_back(b);
        }
    }
    // Sort by block sequence number
    std::sort(blocks.begin(), blocks.end(),
        [](const BlockInfo& a, const BlockInfo& b) {
            return a.blockSeq < b.blockSeq;
        });

    if (blocks.empty()) return "";

    // Open blocks.dat for reading
    std::ifstream blocksFile(m_blocksPath, std::ios::binary);
    if (!blocksFile.is_open()) return "";

    std::string result;
    int64_t bytesRemaining = length;
    int64_t currentOffset = 0;  // current position in the logical file

    for (const auto& block : blocks) {
        int64_t blockStart = currentOffset;
        int64_t blockEnd = currentOffset + block.length;

        // Skip blocks before the requested range
        if (blockEnd <= offset) {
            currentOffset = blockEnd;
            continue;
        }

        // Stop if we've passed the requested range
        if (blockStart >= offset + length) {
            break;
        }

        // Calculate overlap between [blockStart, blockEnd) and [offset, offset+length)
        int64_t readStart = (std::max)(blockStart, offset);
        int64_t readEnd = (std::min)(blockEnd, offset + length);
        int readLen = (int)(readEnd - readStart);

        // Seek to the right position within this block in blocks.dat
        int64_t blockOffset = block.offset + (readStart - blockStart);
        blocksFile.seekg(blockOffset, std::ios::beg);
        if (!blocksFile.good()) {
            currentOffset = blockEnd;
            continue;
        }

        std::string chunk(readLen, '\0');
        blocksFile.read(&chunk[0], readLen);
        result += chunk;

        bytesRemaining -= readLen;
        currentOffset = blockEnd;

        if (bytesRemaining <= 0) break;
    }

    return result;
}

void FileStorage::deleteFile(int64_t fileId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Read all lines from the index file
    std::ifstream idxIn(m_indexPath);
    if (!idxIn.is_open()) return;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(idxIn, line)) {
        lines.push_back(line);
    }
    idxIn.close();

    // Rewrite the index file, prefixing matching entries with '#'
    std::ofstream idxOut(m_indexPath, std::ios::trunc);
    if (!idxOut.is_open()) return;

    for (const auto& l : lines) {
        // Keep already-deleted entries as-is
        if (l.empty() || l[0] == '#') {
            idxOut << l << "\n";
            continue;
        }

        // Parse first field (file_id) from "file_id,block_seq,..."
        std::istringstream iss(l);
        int64_t fid;
        char comma;
        iss >> fid >> comma;

        if (!iss.fail() && fid == fileId) {
            // Mark as deleted by prefixing with #
            idxOut << "#" << l << "\n";
        } else {
            idxOut << l << "\n";
        }
    }
    idxOut.close();
}

std::string FileStorage::createTempFile(int64_t fileId) {
    std::string tempDir = m_basePath + "temp\\";
    CreateDirectoryA(tempDir.c_str(), NULL);

    char buf[256];
    snprintf(buf, sizeof(buf), "%s%lld_%lld.tmp",
             tempDir.c_str(),
             (long long)fileId,
             (long long)time(nullptr));
    return std::string(buf);
}

std::vector<BlockInfo> FileStorage::commitTempFile(int64_t fileId, const std::string& tempPath) {
    std::vector<BlockInfo> result;

    // Read the entire temp file into memory
    std::ifstream tempFile(tempPath, std::ios::binary | std::ios::ate);
    if (!tempFile.is_open()) return result;

    std::streamsize fileSize = tempFile.tellg();
    if (fileSize <= 0) {
        tempFile.close();
        remove(tempPath.c_str());
        return result;
    }

    tempFile.seekg(0, std::ios::beg);
    std::vector<char> fileData((size_t)fileSize);
    tempFile.read(fileData.data(), fileSize);
    tempFile.close();

    // Split into blocks and write each via writeBlock
    const int BLOCK_SIZE = 4096;
    int blockSeq = 0;
    int64_t totalWritten = 0;

    while (totalWritten < fileSize) {
        int blockLen = (int)std::min((int64_t)BLOCK_SIZE, fileSize - totalWritten);

        int64_t offset = writeBlock(fileId, blockSeq,
                                    fileData.data() + totalWritten, blockLen);

        BlockInfo info;
        info.fileId   = fileId;
        info.blockSeq = blockSeq;
        info.offset   = offset;
        info.length   = blockLen;
        result.push_back(info);

        totalWritten += blockLen;
        blockSeq++;
    }

    // Clean up temp file
    remove(tempPath.c_str());

    return result;
}
