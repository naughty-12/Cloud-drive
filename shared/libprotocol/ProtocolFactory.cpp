/**
 * @file ProtocolFactory.cpp
 * @brief 全部协议序列化/反序列化方法的实现。
 *
 * 每个序列化方法遵循相同模式：
 *   1. 创建 BinaryStream。
 *   2. 首先写入 m_ntype（单个字节）。
 *   3. 按顺序用 operator<< 或具名方法写入每个结构体字段。
 *   4. 返回 BinaryStream 原始字节（加帧由网络层完成）。
 *
 * 每个反序列化方法：
 *   1. BinaryStream::fromData(body, len)。
 *   2. 按序列化时的相同顺序读取每个字段。
 *   3. 返回填充完成的结构体。
 */

#include "ProtocolFactory.h"

#include "BinaryStream.h"

#include <cstring>
#include <stdexcept>

// ============================================================================
// parseType / wrapPacket
// ============================================================================

char ProtocolFactory::parseType(const char* packetBody, int bodyLen)
{
    if (!packetBody || bodyLen < 1) {
        throw std::runtime_error("ProtocolFactory::parseType: invalid packet body");
    }
    return packetBody[0];
}

std::vector<uint8_t> ProtocolFactory::wrapPacket(const std::vector<uint8_t>& body)
{
    // 4 字节大端长度前缀 + 包体
    uint32_t bodyLen = static_cast<uint32_t>(body.size());
    // 大端编码（无平台依赖）
    uint8_t lenBytes[4] = {
        (uint8_t)(bodyLen >> 24), (uint8_t)(bodyLen >> 16),
        (uint8_t)(bodyLen >> 8),  (uint8_t)(bodyLen)
    };

    std::vector<uint8_t> packet;
    packet.reserve(4 + body.size());
    packet.insert(packet.end(), lenBytes, lenBytes + 4);
    packet.insert(packet.end(), body.begin(), body.end());

    return packet;
}

// ============================================================================
// FILEINFO 辅助函数
// ============================================================================

void ProtocolFactory::serializeFileInfo(BinaryStream& bs, const FILEINFO& info)
{
    bs.writeFixedString(info.m_szFileName, MAXSIZE);
    bs << info.m_filesize;
    bs.writeFixedString(info.m_szFileUploadTime, MAXSIZE);
    bs << info.m_fileID;
}

FILEINFO ProtocolFactory::deserializeFileInfo(BinaryStream& bs)
{
    FILEINFO info;
    std::memset(&info, 0, sizeof(info));
    bs.readFixedString(info.m_szFileName, MAXSIZE);
    bs >> info.m_filesize;
    bs.readFixedString(info.m_szFileUploadTime, MAXSIZE);
    bs >> info.m_fileID;
    return info;
}

// ============================================================================
// 1. 注册
// ============================================================================

std::vector<uint8_t> ProtocolFactory::serializeRegisterRQ(const STRU_REGISTERRQ& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_tel;
    bs.writeFixedString(s.m_szName, MAXSIZE);
    bs.writeFixedString(s.m_szPasswordSHA256, 65);
    return bs.data();
}

std::vector<uint8_t> ProtocolFactory::serializeRegisterRS(const STRU_REGISTERRS& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs.writeRaw(&s.m_szResult, 1);
    return bs.data();
}

STRU_REGISTERRQ ProtocolFactory::deserializeRegisterRQ(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_REGISTERRQ s;
    bs >> s.m_tel;
    bs.readFixedString(s.m_szName, MAXSIZE);
    bs.readFixedString(s.m_szPasswordSHA256, 65);
    return s;
}

STRU_REGISTERRS ProtocolFactory::deserializeRegisterRS(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_REGISTERRS s;
    bs.readRaw(&s.m_szResult, 1);
    return s;
}

// ============================================================================
// 2. 登录
// ============================================================================

std::vector<uint8_t> ProtocolFactory::serializeLoginRQ(const STRU_LOGINRQ& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs.writeFixedString(s.m_szName, MAXSIZE);
    bs.writeFixedString(s.m_szPasswordSHA256, 65);
    return bs.data();
}

std::vector<uint8_t> ProtocolFactory::serializeLoginRS(const STRU_LOGINRS& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_userId;
    bs.writeRaw(&s.m_szResult, 1);
    return bs.data();
}

STRU_LOGINRQ ProtocolFactory::deserializeLoginRQ(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_LOGINRQ s;
    bs.readFixedString(s.m_szName, MAXSIZE);
    bs.readFixedString(s.m_szPasswordSHA256, 65);
    return s;
}

STRU_LOGINRS ProtocolFactory::deserializeLoginRS(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_LOGINRS s;
    bs >> s.m_userId;
    bs.readRaw(&s.m_szResult, 1);
    return s;
}

// ============================================================================
// 3. 获取文件列表
// ============================================================================

std::vector<uint8_t> ProtocolFactory::serializeGetFileListRQ(const STRU_GETFILELISTRQ& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_userId;
    return bs.data();
}

std::vector<uint8_t> ProtocolFactory::serializeGetFileListRS(const STRU_GETFILELISTRS& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << static_cast<int32_t>(s.m_nFileNum);
    for (int i = 0; i < s.m_nFileNum && i < MAXSIZE; ++i) {
        serializeFileInfo(bs, s.m_aryFileInfo[i]);
    }
    return bs.data();
}

STRU_GETFILELISTRQ ProtocolFactory::deserializeGetFileListRQ(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_GETFILELISTRQ s;
    bs >> s.m_userId;
    return s;
}

STRU_GETFILELISTRS ProtocolFactory::deserializeGetFileListRS(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_GETFILELISTRS s;
    int32_t fileNum = 0;
    bs >> fileNum;
    s.m_nFileNum = static_cast<int>(fileNum);
    for (int i = 0; i < s.m_nFileNum && i < MAXSIZE; ++i) {
        s.m_aryFileInfo[i] = deserializeFileInfo(bs);
    }
    return s;
}

// ============================================================================
// 4. 上传文件信息
// ============================================================================

std::vector<uint8_t> ProtocolFactory::serializeUploadFileInfoRQ(const STRU_UPLOADFILEINFORQ& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_userId;
    serializeFileInfo(bs, s.m_fileInfo);
    bs.writeFixedString(s.m_szFileSHA256, 65);
    return bs.data();
}

std::vector<uint8_t> ProtocolFactory::serializeUploadFileInfoRS(const STRU_UPLOADFILEINFORS& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs.writeFixedString(s.m_szFileName, MAXSIZE);
    bs.writeFixedString(s.m_szFileSHA256, 65);
    bs << s.m_fileID;
    bs << s.m_pos;
    bs.writeRaw(&s.m_szResult, 1);
    return bs.data();
}

STRU_UPLOADFILEINFORQ ProtocolFactory::deserializeUploadFileInfoRQ(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_UPLOADFILEINFORQ s;
    bs >> s.m_userId;
    s.m_fileInfo = deserializeFileInfo(bs);
    bs.readFixedString(s.m_szFileSHA256, 65);
    return s;
}

STRU_UPLOADFILEINFORS ProtocolFactory::deserializeUploadFileInfoRS(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_UPLOADFILEINFORS s;
    bs.readFixedString(s.m_szFileName, MAXSIZE);
    bs.readFixedString(s.m_szFileSHA256, 65);
    bs >> s.m_fileID;
    bs >> s.m_pos;
    bs.readRaw(&s.m_szResult, 1);
    return s;
}

// ============================================================================
// 5. 上传文件块
// ============================================================================

std::vector<uint8_t> ProtocolFactory::serializeUploadFileBlockRQ(const STRU_UPLOADFILEBLOCKRQ& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_userId;
    bs << s.m_fileID;
    bs << s.m_blockSeq;   // F4-6：显式块序号
    // 文件内容：先写实际大小（int64_t），再写原始字节
    bs << s.m_fileblocksize;
    if (s.m_fileblocksize > 0) {
        bs.writeRaw(s.m_szFileContent, static_cast<int>(s.m_fileblocksize));
    }
    return bs.data();
}

std::vector<uint8_t> ProtocolFactory::serializeUploadFileBlockRS(const STRU_UPLOADFILEBLOCKRS& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_fileID;
    bs << s.m_pos;
    bs.writeRaw(&s.m_szResult, 1);
    return bs.data();
}

STRU_UPLOADFILEBLOCKRQ ProtocolFactory::deserializeUploadFileBlockRQ(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_UPLOADFILEBLOCKRQ s;
    bs >> s.m_userId;
    bs >> s.m_fileID;
    bs >> s.m_blockSeq;   // F4-6：显式块序号
    bs >> s.m_fileblocksize;
    if (s.m_fileblocksize > 0 && s.m_fileblocksize <= MAXFILECONTENT) {
        bs.readRaw(s.m_szFileContent, static_cast<int>(s.m_fileblocksize));
    } else if (s.m_fileblocksize > MAXFILECONTENT) {
        // 截断保护：最多只读取 MAXFILECONTENT
        bs.readRaw(s.m_szFileContent, MAXFILECONTENT);
        s.m_fileblocksize = MAXFILECONTENT;
    }
    return s;
}

STRU_UPLOADFILEBLOCKRS ProtocolFactory::deserializeUploadFileBlockRS(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_UPLOADFILEBLOCKRS s;
    bs >> s.m_fileID;
    bs >> s.m_pos;
    bs.readRaw(&s.m_szResult, 1);
    return s;
}

// ============================================================================
// 6. 下载文件信息
// ============================================================================

std::vector<uint8_t> ProtocolFactory::serializeDownloadFileInfoRQ(const STRU_DOWNLOADFILEINFORQ& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_userId;
    bs << s.m_fileID;
    bs.writeFixedString(s.m_szFileName, MAXSIZE);
    return bs.data();
}

std::vector<uint8_t> ProtocolFactory::serializeDownloadFileInfoRS(const STRU_DOWNLOADFILEINFORS& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_fileID;
    bs << s.m_fileSize;
    bs << static_cast<int32_t>(s.m_nBlockNum);
    bs << s.m_nBlockSize;
    bs.writeFixedString(s.m_szFileSHA256, 65);
    bs.writeRaw(&s.m_szResult, 1);
    return bs.data();
}

STRU_DOWNLOADFILEINFORQ ProtocolFactory::deserializeDownloadFileInfoRQ(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_DOWNLOADFILEINFORQ s;
    bs >> s.m_userId;
    bs >> s.m_fileID;
    bs.readFixedString(s.m_szFileName, MAXSIZE);
    return s;
}

STRU_DOWNLOADFILEINFORS ProtocolFactory::deserializeDownloadFileInfoRS(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_DOWNLOADFILEINFORS s;
    bs >> s.m_fileID;
    bs >> s.m_fileSize;
    int32_t blockNum = 0;
    bs >> blockNum;
    s.m_nBlockNum = static_cast<int>(blockNum);
    bs >> s.m_nBlockSize;
    bs.readFixedString(s.m_szFileSHA256, 65);
    bs.readRaw(&s.m_szResult, 1);
    return s;
}

// ============================================================================
// 7. 下载文件块
// ============================================================================

std::vector<uint8_t> ProtocolFactory::serializeDownloadFileBlockRQ(const STRU_DOWNLOADFILEBLOCKRQ& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_userId;
    bs << s.m_fileID;
    bs << s.m_pos;
    return bs.data();
}

std::vector<uint8_t> ProtocolFactory::serializeDownloadFileBlockRS(const STRU_DOWNLOADFILEBLOCKRS& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_fileID;
    bs << s.m_fileblocksize;
    if (s.m_fileblocksize > 0) {
        bs.writeRaw(s.m_szFileContent, static_cast<int>(s.m_fileblocksize));
    }
    bs << s.m_pos;
    bs.writeRaw(&s.m_szResult, 1);
    return bs.data();
}

STRU_DOWNLOADFILEBLOCKRQ ProtocolFactory::deserializeDownloadFileBlockRQ(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_DOWNLOADFILEBLOCKRQ s;
    bs >> s.m_userId;
    bs >> s.m_fileID;
    bs >> s.m_pos;
    return s;
}

STRU_DOWNLOADFILEBLOCKRS ProtocolFactory::deserializeDownloadFileBlockRS(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_DOWNLOADFILEBLOCKRS s;
    bs >> s.m_fileID;
    bs >> s.m_fileblocksize;
    if (s.m_fileblocksize > 0 && s.m_fileblocksize <= MAXFILECONTENT) {
        bs.readRaw(s.m_szFileContent, static_cast<int>(s.m_fileblocksize));
    } else if (s.m_fileblocksize > MAXFILECONTENT) {
        // 截断保护：最多只读取 MAXFILECONTENT
        bs.readRaw(s.m_szFileContent, MAXFILECONTENT);
        s.m_fileblocksize = MAXFILECONTENT;
    }
    bs >> s.m_pos;
    bs.readRaw(&s.m_szResult, 1);
    return s;
}

// ============================================================================
// 8. 搜索文件
// ============================================================================

std::vector<uint8_t> ProtocolFactory::serializeSearchFileRQ(const STRU_SEARCHFILERQ& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_userId;
    bs.writeFixedString(s.m_szSearchKey, MAXSIZE);
    return bs.data();
}

std::vector<uint8_t> ProtocolFactory::serializeSearchFileRS(const STRU_SEARCHFILERS& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << static_cast<int32_t>(s.m_nFileNum);
    for (int i = 0; i < s.m_nFileNum && i < MAXSIZE; ++i) {
        serializeFileInfo(bs, s.m_aryFileInfo[i]);
    }
    return bs.data();
}

STRU_SEARCHFILERQ ProtocolFactory::deserializeSearchFileRQ(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_SEARCHFILERQ s;
    bs >> s.m_userId;
    bs.readFixedString(s.m_szSearchKey, MAXSIZE);
    return s;
}

STRU_SEARCHFILERS ProtocolFactory::deserializeSearchFileRS(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_SEARCHFILERS s;
    int32_t fileNum = 0;
    bs >> fileNum;
    s.m_nFileNum = static_cast<int>(fileNum);
    for (int i = 0; i < s.m_nFileNum && i < MAXSIZE; ++i) {
        s.m_aryFileInfo[i] = deserializeFileInfo(bs);
    }
    return s;
}

// ============================================================================
// 9. 删除文件
// ============================================================================

std::vector<uint8_t> ProtocolFactory::serializeDeleteFileRQ(const STRU_DELETEFILERQ& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_userId;
    bs << s.m_fileID;
    return bs.data();
}

std::vector<uint8_t> ProtocolFactory::serializeDeleteFileRS(const STRU_DELETEFILERS& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_fileID;
    bs.writeRaw(&s.m_szResult, 1);
    return bs.data();
}

STRU_DELETEFILERQ ProtocolFactory::deserializeDeleteFileRQ(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_DELETEFILERQ s;
    bs >> s.m_userId;
    bs >> s.m_fileID;
    return s;
}

STRU_DELETEFILERS ProtocolFactory::deserializeDeleteFileRS(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_DELETEFILERS s;
    bs >> s.m_fileID;
    bs.readRaw(&s.m_szResult, 1);
    return s;
}

// ============================================================================
// 10. 分享文件
// ============================================================================

std::vector<uint8_t> ProtocolFactory::serializeShareFileRQ(const STRU_SHAREFILERQ& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_userId;
    bs << s.m_fileID;
    bs.writeFixedString(s.m_szShareToUser, MAXSIZE);
    return bs.data();
}

std::vector<uint8_t> ProtocolFactory::serializeShareFileRS(const STRU_SHAREFILERS& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_fileID;
    bs.writeFixedString(s.m_szShareCode, 9);
    bs.writeRaw(&s.m_szResult, 1);
    return bs.data();
}

STRU_SHAREFILERQ ProtocolFactory::deserializeShareFileRQ(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_SHAREFILERQ s;
    bs >> s.m_userId;
    bs >> s.m_fileID;
    bs.readFixedString(s.m_szShareToUser, MAXSIZE);
    return s;
}

STRU_SHAREFILERS ProtocolFactory::deserializeShareFileRS(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_SHAREFILERS s;
    bs >> s.m_fileID;
    bs.readFixedString(s.m_szShareCode, 9);
    bs.readRaw(&s.m_szResult, 1);
    return s;
}

// ---------------------------------------------------------------------------
// 10.5 撤销分享（F10-4：分享撤销）
// ---------------------------------------------------------------------------

std::vector<uint8_t> ProtocolFactory::serializeDeleteShareRQ(const STRU_DELETESHARERQ& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_userId;
    bs << s.m_fileID;
    return bs.data();
}

std::vector<uint8_t> ProtocolFactory::serializeDeleteShareRS(const STRU_DELETESHARERS& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_fileID;
    bs.writeRaw(&s.m_szResult, 1);
    return bs.data();
}

STRU_DELETESHARERQ ProtocolFactory::deserializeDeleteShareRQ(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_DELETESHARERQ s;
    bs >> s.m_userId;
    bs >> s.m_fileID;
    return s;
}

STRU_DELETESHARERS ProtocolFactory::deserializeDeleteShareRS(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_DELETESHARERS s;
    bs >> s.m_fileID;
    bs.readRaw(&s.m_szResult, 1);
    return s;
}

// ============================================================================
// 10.6 稀疏指纹预检（秒传 L2 漏斗）
// ============================================================================

std::vector<uint8_t> ProtocolFactory::serializeSparseCheckRQ(const STRU_SPARSECHECKRQ& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_userId;
    bs << s.m_fileSize;
    bs.writeFixedString(s.m_szSparseFingerprint, 65);
    return bs.data();
}

std::vector<uint8_t> ProtocolFactory::serializeSparseCheckRS(const STRU_SPARSECHECKRS& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs.writeRaw(&s.m_szResult, 1);
    return bs.data();
}

STRU_SPARSECHECKRQ ProtocolFactory::deserializeSparseCheckRQ(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_SPARSECHECKRQ s;
    bs >> s.m_userId;
    bs >> s.m_fileSize;
    bs.readFixedString(s.m_szSparseFingerprint, 65);
    return s;
}

STRU_SPARSECHECKRS ProtocolFactory::deserializeSparseCheckRS(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_SPARSECHECKRS s;
    bs.readRaw(&s.m_szResult, 1);
    return s;
}

// ============================================================================
// 11. 获取文件（提取码）
// ============================================================================

std::vector<uint8_t> ProtocolFactory::serializeGetFileRQ(const STRU_GETFILERQ& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_userId;
    bs << s.m_shareFileID;
    return bs.data();
}

std::vector<uint8_t> ProtocolFactory::serializeGetFileRS(const STRU_GETFILERS& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    serializeFileInfo(bs, s.m_fileInfo);
    bs.writeFixedString(s.m_szFileSHA256, 65);
    bs << s.m_fileID;
    bs << s.m_pos;
    bs.writeRaw(&s.m_szResult, 1);
    return bs.data();
}

STRU_GETFILERQ ProtocolFactory::deserializeGetFileRQ(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_GETFILERQ s;
    bs >> s.m_userId;
    bs >> s.m_shareFileID;
    return s;
}

STRU_GETFILERS ProtocolFactory::deserializeGetFileRS(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_GETFILERS s;
    s.m_fileInfo = deserializeFileInfo(bs);
    bs.readFixedString(s.m_szFileSHA256, 65);
    bs >> s.m_fileID;
    bs >> s.m_pos;
    bs.readRaw(&s.m_szResult, 1);
    return s;
}

// ============================================================================
// 12. AI 预览（Phase 3）
// ============================================================================

std::vector<uint8_t> ProtocolFactory::serializeAIPreviewRQ(const STRU_AIPREVIEWRQ& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_userId;
    bs << s.m_fileID;
    return bs.data();
}

std::vector<uint8_t> ProtocolFactory::serializeAIPreviewRS(const STRU_AIPREVIEWRS& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_fileID;
    bs.writeFixedString(s.m_szSummary, SQLLEN * 2);
    bs.writeFixedString(s.m_szKeywords, MAXSIZE * 4);
    bs.writeFixedString(s.m_szKeySentences, SQLLEN * 2);
    bs.writeFixedString(s.m_szFileType, MAXSIZE);
    bs.writeFixedString(s.m_szFileName, MAXSIZE);
    bs << s.m_nRawContentLen;
    bs.writeRaw(s.m_szRawContent, MAXFILECONTENT * 2);
    bs.writeFixedString(s.m_szAIError, 256);
    bs.writeRaw(&s.m_szResult, 1);
    return bs.data();
}

STRU_AIPREVIEWRQ ProtocolFactory::deserializeAIPreviewRQ(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_AIPREVIEWRQ s;
    bs >> s.m_userId;
    bs >> s.m_fileID;
    return s;
}

STRU_AIPREVIEWRS ProtocolFactory::deserializeAIPreviewRS(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_AIPREVIEWRS s;
    bs >> s.m_fileID;
    bs.readFixedString(s.m_szSummary, SQLLEN * 2);
    bs.readFixedString(s.m_szKeywords, MAXSIZE * 4);
    bs.readFixedString(s.m_szKeySentences, SQLLEN * 2);
    bs.readFixedString(s.m_szFileType, MAXSIZE);
    bs.readFixedString(s.m_szFileName, MAXSIZE);
    bs >> s.m_nRawContentLen;
    bs.readRaw(s.m_szRawContent, MAXFILECONTENT * 2);
    bs.readFixedString(s.m_szAIError, 256);
    bs.readRaw(&s.m_szResult, 1);
    return s;
}

// ============================================================================
// 13. AI 语义搜索（Phase 3）
// ============================================================================

std::vector<uint8_t> ProtocolFactory::serializeAISearchRQ(const STRU_AISEARCHRQ& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_userId;
    bs.writeFixedString(s.m_szQuery, SQLLEN);
    return bs.data();
}

std::vector<uint8_t> ProtocolFactory::serializeAISearchRS(const STRU_AISEARCHRS& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << static_cast<int32_t>(s.m_nResultNum);
    for (int i = 0; i < s.m_nResultNum && i < MAXSIZE; ++i) {
        serializeFileInfo(bs, s.m_aryResults[i].m_fileInfo);
        bs.writeFixedString(s.m_aryResults[i].m_szMatchReason, SQLLEN);
        bs.writeFixedString(s.m_aryResults[i].m_szFileSHA256, 65);
    }
    bs.writeRaw(&s.m_szResult, 1);
    return bs.data();
}

STRU_AISEARCHRQ ProtocolFactory::deserializeAISearchRQ(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_AISEARCHRQ s;
    bs >> s.m_userId;
    bs.readFixedString(s.m_szQuery, SQLLEN);
    return s;
}

STRU_AISEARCHRS ProtocolFactory::deserializeAISearchRS(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_AISEARCHRS s;
    int32_t resultNum = 0;
    bs >> resultNum;
    s.m_nResultNum = static_cast<int>(resultNum);
    for (int i = 0; i < s.m_nResultNum && i < MAXSIZE; ++i) {
        s.m_aryResults[i].m_fileInfo = deserializeFileInfo(bs);
        bs.readFixedString(s.m_aryResults[i].m_szMatchReason, SQLLEN);
        bs.readFixedString(s.m_aryResults[i].m_szFileSHA256, 65);
    }
    bs.readRaw(&s.m_szResult, 1);
    return s;
}

// ============================================================================
// 14. AI 自动标签（Phase 3）
// ============================================================================

std::vector<uint8_t> ProtocolFactory::serializeAITagRQ(const STRU_AITAGRQ& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_userId;
    bs << s.m_fileID;
    return bs.data();
}

std::vector<uint8_t> ProtocolFactory::serializeAITagRS(const STRU_AITAGRS& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_fileID;
    bs << static_cast<int32_t>(s.m_nTagNum);
    for (int i = 0; i < s.m_nTagNum && i < 15; ++i) {
        bs.writeFixedString(s.m_szTags[i], MAXSIZE);
    }
    bs << static_cast<int32_t>(s.m_nNewTagSuggestions);
    for (int i = 0; i < s.m_nNewTagSuggestions && i < 5; ++i) {
        bs.writeFixedString(s.m_szNewTags[i], MAXSIZE);
    }
    bs.writeRaw(&s.m_szResult, 1);
    return bs.data();
}

STRU_AITAGRQ ProtocolFactory::deserializeAITagRQ(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_AITAGRQ s;
    bs >> s.m_userId;
    bs >> s.m_fileID;
    return s;
}

STRU_AITAGRS ProtocolFactory::deserializeAITagRS(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_AITAGRS s;
    bs >> s.m_fileID;
    int32_t tagNum = 0;
    bs >> tagNum;
    s.m_nTagNum = static_cast<int>(tagNum);
    for (int i = 0; i < s.m_nTagNum && i < 15; ++i) {
        bs.readFixedString(s.m_szTags[i], MAXSIZE);
    }
    int32_t newTagSug = 0;
    bs >> newTagSug;
    s.m_nNewTagSuggestions = static_cast<int>(newTagSug);
    for (int i = 0; i < s.m_nNewTagSuggestions && i < 5; ++i) {
        bs.readFixedString(s.m_szNewTags[i], MAXSIZE);
    }
    bs.readRaw(&s.m_szResult, 1);
    return s;
}

// ============================================================================
// 15. 流媒体 Token（Phase 2：HTTP 视频/音频流媒体）
// ============================================================================

std::vector<uint8_t> ProtocolFactory::serializeStreamTokenRQ(const STRU_STREAMTOKENRQ& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_userId;
    bs << s.m_fileID;
    return bs.data();
}

std::vector<uint8_t> ProtocolFactory::serializeStreamTokenRS(const STRU_STREAMTOKENRS& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_fileID;
    bs.writeFixedString(s.m_szToken, 65);
    bs << s.m_nTimestamp;
    bs << s.m_nHttpPort;
    bs.writeFixedString(s.m_szFileName, 260);
    bs << s.m_fileSize;
    bs.writeRaw(&s.m_szResult, 1);
    return bs.data();
}

STRU_STREAMTOKENRQ ProtocolFactory::deserializeStreamTokenRQ(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_STREAMTOKENRQ s;
    bs >> s.m_userId;
    bs >> s.m_fileID;
    return s;
}

STRU_STREAMTOKENRS ProtocolFactory::deserializeStreamTokenRS(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_STREAMTOKENRS s;
    bs >> s.m_fileID;
    bs.readFixedString(s.m_szToken, 65);
    bs >> s.m_nTimestamp;
    bs >> s.m_nHttpPort;
    bs.readFixedString(s.m_szFileName, 260);
    bs >> s.m_fileSize;
    bs.readRaw(&s.m_szResult, 1);
    return s;
}

// ============================================================================
// 16. 集群：复制块（分布式 L2）
// ============================================================================

std::vector<uint8_t> ProtocolFactory::serializeReplicateBlockRQ(const STRU_REPLICATEBLOCKRQ& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_fileId;
    bs << static_cast<int32_t>(s.m_blockSeq);
    bs << s.m_offset;
    bs << s.m_dataLen;
    if (s.m_dataLen > 0) {
        bs.writeRaw(s.m_szData, static_cast<int>(s.m_dataLen));
    }
    return bs.data();
}

std::vector<uint8_t> ProtocolFactory::serializeReplicateBlockRS(const STRU_REPLICATEBLOCKRS& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs << s.m_fileId;
    bs << static_cast<int32_t>(s.m_blockSeq);
    bs.writeRaw(&s.m_szResult, 1);
    return bs.data();
}

STRU_REPLICATEBLOCKRQ ProtocolFactory::deserializeReplicateBlockRQ(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_REPLICATEBLOCKRQ s;
    bs >> s.m_fileId;
    int32_t blockSeq = 0;
    bs >> blockSeq;
    s.m_blockSeq = static_cast<int>(blockSeq);
    bs >> s.m_offset;
    bs >> s.m_dataLen;
    if (s.m_dataLen > 0 && s.m_dataLen <= MAXFILECONTENT) {
        bs.readRaw(s.m_szData, static_cast<int>(s.m_dataLen));
    } else if (s.m_dataLen > MAXFILECONTENT) {
        bs.readRaw(s.m_szData, MAXFILECONTENT);
        s.m_dataLen = MAXFILECONTENT;
    }
    return s;
}

STRU_REPLICATEBLOCKRS ProtocolFactory::deserializeReplicateBlockRS(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_REPLICATEBLOCKRS s;
    bs >> s.m_fileId;
    int32_t blockSeq = 0;
    bs >> blockSeq;
    s.m_blockSeq = static_cast<int>(blockSeq);
    bs.readRaw(&s.m_szResult, 1);
    return s;
}

// ============================================================================
// 17. 集群：重定向（分布式 L2）
// ============================================================================

std::vector<uint8_t> ProtocolFactory::serializeRedirectRS(const STRU_REDIRECTRS& s)
{
    BinaryStream bs;
    bs.writeRaw(&s.m_ntype, 1);
    bs.writeFixedString(s.m_szRedirectIP, 16);
    bs << static_cast<int32_t>(s.m_nRedirectPort);
    bs.writeRaw(&s.m_szResult, 1);
    return bs.data();
}

STRU_REDIRECTRS ProtocolFactory::deserializeRedirectRS(const char* body, int len)
{
    BinaryStream bs = BinaryStream::fromData(body, len);
    STRU_REDIRECTRS s;
    bs.readFixedString(s.m_szRedirectIP, 16);
    int32_t port = 0;
    bs >> port;
    s.m_nRedirectPort = static_cast<int>(port);
    bs.readRaw(&s.m_szResult, 1);
    return s;
}
