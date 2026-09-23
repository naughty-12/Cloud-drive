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
    // 确保以反斜杠结尾
    if (!m_basePath.empty() && m_basePath.back() != '\\') {
        m_basePath += '\\';
    }

    m_blocksPath = m_basePath + "blocks.dat";
    m_indexPath  = m_basePath + "blocks.idx";

    // 创建基础目录（目录已存在时 CreateDirectoryA 是幂等的）
    CreateDirectoryA(m_basePath.c_str(), NULL);

    // 创建临时目录
    std::string tempDir = m_basePath + "temp\\";
    CreateDirectoryA(tempDir.c_str(), NULL);

    // 以追加+写入模式打开 blocks.dat。
    // 若文件不存在，先创建它。
    struct stat st;
    bool exists = (stat(m_blocksPath.c_str(), &st) == 0);
    if (!exists) {
        // 创建文件
        std::ofstream create(m_blocksPath, std::ios::binary);
        create.close();
    }

    m_blocksStream.open(m_blocksPath, std::ios::app | std::ios::binary);
    if (!m_blocksStream.is_open()) {
        return false;
    }

    // 加载已有索引并计算总大小
    auto blocks = readAllIndex();
    m_totalSize = 0;
    for (const auto& b : blocks) {
        int64_t end = b.offset + b.length;
        if (end > m_totalSize) m_totalSize = end;
    }

    return true;
}

void FileStorage::writeIndexEntry(int64_t fileId, int blockSeq, int64_t offset, int length) {
    // 以追加模式打开索引文件以添加一行
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
        // 跳过空行与已删除条目（以 # 开头）
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

    // F16-4 修复：幂等性——若 (fileId, blockSeq) 已写入，直接返回已有偏移
    {
        auto blocks = readAllIndex();
        for (const auto& b : blocks) {
            if (b.fileId == fileId && b.blockSeq == blockSeq) {
                return b.offset;  // 已存在——不重复写入
            }
        }
    }

    // 定位到 blocks.dat 末尾以获取当前写入偏移。
    // 追加模式下，seekp(0, end) + tellp() 得到写入前的位置。
    m_blocksStream.seekp(0, std::ios::end);
    int64_t offset = m_blocksStream.tellp();

    // 写入数据
    m_blocksStream.write(data, len);
    m_blocksStream.flush();

    // 记录索引条目
    writeIndexEntry(fileId, blockSeq, offset, len);

    // 更新总大小
    m_totalSize = offset + len;

    return offset;
}

std::string FileStorage::readBlock(int64_t fileId, int blockSeq) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto blocks = readAllIndex();
    for (const auto& b : blocks) {
        if (b.fileId == fileId && b.blockSeq == blockSeq) {
            // 为数据打开独立的只读流
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
    // 按块序号排序
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

    // 获取该文件的已排序块
    auto allBlocks = readAllIndex();
    std::vector<BlockInfo> blocks;
    for (const auto& b : allBlocks) {
        if (b.fileId == fileId) {
            blocks.push_back(b);
        }
    }
    // 按块序号排序
    std::sort(blocks.begin(), blocks.end(),
        [](const BlockInfo& a, const BlockInfo& b) {
            return a.blockSeq < b.blockSeq;
        });

    if (blocks.empty()) return "";

    // 打开 blocks.dat 用于读取
    std::ifstream blocksFile(m_blocksPath, std::ios::binary);
    if (!blocksFile.is_open()) return "";

    std::string result;
    int64_t bytesRemaining = length;
    int64_t currentOffset = 0;  // 逻辑文件中的当前位置

    for (const auto& block : blocks) {
        int64_t blockStart = currentOffset;
        int64_t blockEnd = currentOffset + block.length;

        // 跳过请求区间之前的块
        if (blockEnd <= offset) {
            currentOffset = blockEnd;
            continue;
        }

        // 已越过请求区间则停止
        if (blockStart >= offset + length) {
            break;
        }

        // 计算 [blockStart, blockEnd) 与 [offset, offset+length) 的重叠部分
        int64_t readStart = (std::max)(blockStart, offset);
        int64_t readEnd = (std::min)(blockEnd, offset + length);
        int readLen = (int)(readEnd - readStart);

        // 在 blocks.dat 中定位到该块内的正确位置
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

    // 读取索引文件的全部行
    std::ifstream idxIn(m_indexPath);
    if (!idxIn.is_open()) return;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(idxIn, line)) {
        lines.push_back(line);
    }
    idxIn.close();

    // 重写索引文件，将匹配条目加上 '#' 前缀
    std::ofstream idxOut(m_indexPath, std::ios::trunc);
    if (!idxOut.is_open()) return;

    for (const auto& l : lines) {
        // 已删除的条目原样保留
        if (l.empty() || l[0] == '#') {
            idxOut << l << "\n";
            continue;
        }

        // 从 "file_id,block_seq,..." 解析第一个字段（file_id）
        std::istringstream iss(l);
        int64_t fid;
        char comma;
        iss >> fid >> comma;

        if (!iss.fail() && fid == fileId) {
            // 通过加 # 前缀标记为已删除
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

    // 将整个临时文件读入内存
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

    // 分块并通过 writeBlock 逐块写入
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

    // 清理临时文件
    remove(tempPath.c_str());

    return result;
}
