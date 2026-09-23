/**
 * @file tst_protocolfactory.cpp
 * @brief ProtocolFactory 协议工厂单元测试。
 *
 * 所有序列化产物格式: [4 字节大端长度][1 字节 m_ntype][字段...]
 * 反序列化入参: 去掉 4 字节长度前缀与 m_ntype 之后的 body。
 */

#include "tst_protocolfactory.h"

#include <QtTest/QtTest>

#include "ProtocolFactory.h"

#include <cstring>
#include <string>
#include <vector>

using std::string;
using std::vector;

// ---------------------------------------------------------------------------
// 往返辅助: 序列化 → 校验类型 → 反序列化
// 注意: serialize*() 产物 = [m_ntype][字段...], 不包含 4 字节长度前缀
//       (加帧由网络层 / wrapPacket() 完成, 见 ProtocolFactory.cpp 头注释)。
//       QVERIFY/QCOMPARE 宏展开为 "return;", 因此本函数必须返回 void,
//       结果通过输出参数返回 (Qt Test 宏不能在非 void 函数中使用)。
// ---------------------------------------------------------------------------
template <typename T, typename Ser, typename De>
void roundTripPacket(const T& src, char expectedType, Ser ser, De de, T* out)
{
    vector<uint8_t> pkt = ser(src);
    QVERIFY(static_cast<int>(pkt.size()) >= 1);   // 至少 m_ntype 一个字节

    // m_ntype 在 body 首字节
    char type = ProtocolFactory::parseType(
        reinterpret_cast<const char*>(pkt.data()),
        static_cast<int>(pkt.size()));
    QCOMPARE(type, expectedType);

    // 反序列化 (跳过 m_ntype)
    *out = de(reinterpret_cast<const char*>(pkt.data()) + 1,
              static_cast<int>(pkt.size()) - 1);
}

// ---------------------------------------------------------------------------
// parseType
// ---------------------------------------------------------------------------
void TestProtocolFactory::parseTypeBasics()
{
    // 首字节即类型
    const char body[] = { static_cast<char>(37), '\x01', '\x02' };
    QCOMPARE(ProtocolFactory::parseType(body, 3), static_cast<char>(37));

    // 空 body → 抛异常
    QVERIFY_EXCEPTION_THROWN(ProtocolFactory::parseType(body, 0), std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(ProtocolFactory::parseType(nullptr, 3), std::runtime_error);
}

// ---------------------------------------------------------------------------
// wrapPacket
// ---------------------------------------------------------------------------
void TestProtocolFactory::wrapPacketBasics()
{
    const vector<uint8_t> body = { 0xAA, 0xBB, 0xCC };
    vector<uint8_t> pkt = ProtocolFactory::wrapPacket(body);

    QCOMPARE(static_cast<int>(pkt.size()), 7);
    // 大端长度前缀 3
    QCOMPARE(static_cast<int>(pkt[0]), 0);
    QCOMPARE(static_cast<int>(pkt[1]), 0);
    QCOMPARE(static_cast<int>(pkt[2]), 0);
    QCOMPARE(static_cast<int>(pkt[3]), 3);
    // body 原样
    QVERIFY(vector<uint8_t>(pkt.begin() + 4, pkt.end()) == body);

    // 空 body
    vector<uint8_t> empty = ProtocolFactory::wrapPacket(vector<uint8_t>());
    QCOMPARE(static_cast<int>(empty.size()), 4);
    QCOMPARE(static_cast<int>(empty[3]), 0);
}

// ---------------------------------------------------------------------------
// 注册 RQ/RS (2/3)
// ---------------------------------------------------------------------------
void TestProtocolFactory::registerRoundTrip()
{
    STRU_REGISTERRQ rq;
    rq.m_tel = 13800138000LL;
    strcpy(rq.m_szName, "alice");
    strcpy(rq.m_szPasswordSHA256, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    STRU_REGISTERRQ back;
    roundTripPacket(
        rq, static_cast<char>(_default_protocol_register_rq),
        ProtocolFactory::serializeRegisterRQ, ProtocolFactory::deserializeRegisterRQ, &back);

    QCOMPARE(back.m_tel, rq.m_tel);
    QCOMPARE(string(back.m_szName), string(rq.m_szName));
    QCOMPARE(string(back.m_szPasswordSHA256), string(rq.m_szPasswordSHA256));
    QCOMPARE(back.m_ntype, rq.m_ntype);

    STRU_REGISTERRS rs;
    rs.m_szResult = _register_success;
    STRU_REGISTERRS rsBack;
    roundTripPacket(
        rs, static_cast<char>(_default_protocol_register_rs),
        ProtocolFactory::serializeRegisterRS, ProtocolFactory::deserializeRegisterRS, &rsBack);
    QCOMPARE(static_cast<int>(rsBack.m_szResult), _register_success);
}

// ---------------------------------------------------------------------------
// 登录 RQ/RS (4/5)
// ---------------------------------------------------------------------------
void TestProtocolFactory::loginRoundTrip()
{
    STRU_LOGINRQ rq;
    strcpy(rq.m_szName, "bob");
    strcpy(rq.m_szPasswordSHA256, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    STRU_LOGINRQ back;
    roundTripPacket(
        rq, static_cast<char>(_default_protocol_login_rq),
        ProtocolFactory::serializeLoginRQ, ProtocolFactory::deserializeLoginRQ, &back);
    QCOMPARE(string(back.m_szName), string(rq.m_szName));
    QCOMPARE(string(back.m_szPasswordSHA256), string(rq.m_szPasswordSHA256));

    STRU_LOGINRS rs;
    rs.m_userId = 7;
    rs.m_szResult = _login_success;
    STRU_LOGINRS rsBack;
    roundTripPacket(
        rs, static_cast<char>(_default_protocol_login_rs),
        ProtocolFactory::serializeLoginRS, ProtocolFactory::deserializeLoginRS, &rsBack);
    QCOMPARE(rsBack.m_userId, static_cast<int64_t>(7));
    QCOMPARE(static_cast<int>(rsBack.m_szResult), _login_success);
}

// ---------------------------------------------------------------------------
// 文件列表 RQ/RS (6/7) — 多 FILEINFO
// ---------------------------------------------------------------------------
void TestProtocolFactory::getFileListRoundTrip()
{
    STRU_GETFILELISTRQ rq;
    rq.m_userId = 42;
    STRU_GETFILELISTRQ back;
    roundTripPacket(
        rq, static_cast<char>(_default_protocol_getfilelist_rq),
        ProtocolFactory::serializeGetFileListRQ, ProtocolFactory::deserializeGetFileListRQ, &back);
    QCOMPARE(back.m_userId, static_cast<int64_t>(42));

    STRU_GETFILELISTRS rs;
    memset(&rs, 0, sizeof(rs));
    rs.m_ntype = static_cast<char>(_default_protocol_getfilelist_rs);
    rs.m_nFileNum = 2;
    strcpy(rs.m_aryFileInfo[0].m_szFileName, "report.txt");
    rs.m_aryFileInfo[0].m_filesize = 1024;
    strcpy(rs.m_aryFileInfo[0].m_szFileUploadTime, "2026-08-20 10:00:00");
    rs.m_aryFileInfo[0].m_fileID = 11;
    strcpy(rs.m_aryFileInfo[1].m_szFileName, "照片.jpg");
    rs.m_aryFileInfo[1].m_filesize = 2048576;
    strcpy(rs.m_aryFileInfo[1].m_szFileUploadTime, "2026-08-19 09:30:00");
    rs.m_aryFileInfo[1].m_fileID = 12;

    STRU_GETFILELISTRS rsBack;
    roundTripPacket(
        rs, static_cast<char>(_default_protocol_getfilelist_rs),
        ProtocolFactory::serializeGetFileListRS, ProtocolFactory::deserializeGetFileListRS, &rsBack);
    QCOMPARE(rsBack.m_nFileNum, 2);
    QCOMPARE(string(rsBack.m_aryFileInfo[0].m_szFileName), string("report.txt"));
    QCOMPARE(rsBack.m_aryFileInfo[0].m_filesize, static_cast<int64_t>(1024));
    QCOMPARE(string(rsBack.m_aryFileInfo[0].m_szFileUploadTime), string("2026-08-20 10:00:00"));
    QCOMPARE(rsBack.m_aryFileInfo[0].m_fileID, static_cast<int64_t>(11));
    QCOMPARE(string(rsBack.m_aryFileInfo[1].m_szFileName), string("照片.jpg"));
    QCOMPARE(rsBack.m_aryFileInfo[1].m_filesize, static_cast<int64_t>(2048576));
    QCOMPARE(rsBack.m_aryFileInfo[1].m_fileID, static_cast<int64_t>(12));
}

// ---------------------------------------------------------------------------
// 上传信息 RQ/RS (8/9)
// ---------------------------------------------------------------------------
void TestProtocolFactory::uploadFileInfoRoundTrip()
{
    STRU_UPLOADFILEINFORQ rq;
    memset(&rq, 0, sizeof(rq));
    rq.m_ntype = static_cast<char>(_default_protocol_uploadfileinfo_rq);
    rq.m_userId = 3;
    strcpy(rq.m_fileInfo.m_szFileName, "archive.zip");
    rq.m_fileInfo.m_filesize = 8388608;
    strcpy(rq.m_fileInfo.m_szFileUploadTime, "2026-08-20 12:00:00");
    rq.m_fileInfo.m_fileID = 99;
    strcpy(rq.m_szFileSHA256, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    STRU_UPLOADFILEINFORQ back;
    roundTripPacket(
        rq, static_cast<char>(_default_protocol_uploadfileinfo_rq),
        ProtocolFactory::serializeUploadFileInfoRQ, ProtocolFactory::deserializeUploadFileInfoRQ, &back);
    QCOMPARE(back.m_userId, static_cast<int64_t>(3));
    QCOMPARE(string(back.m_fileInfo.m_szFileName), string("archive.zip"));
    QCOMPARE(back.m_fileInfo.m_filesize, static_cast<int64_t>(8388608));
    QCOMPARE(string(back.m_fileInfo.m_szFileUploadTime), string("2026-08-20 12:00:00"));
    QCOMPARE(back.m_fileInfo.m_fileID, static_cast<int64_t>(99));
    QCOMPARE(string(back.m_szFileSHA256), string(rq.m_szFileSHA256));

    STRU_UPLOADFILEINFORS rs;
    memset(&rs, 0, sizeof(rs));
    rs.m_ntype = static_cast<char>(_default_protocol_uploadfileinfo_rs);
    strcpy(rs.m_szFileName, "archive.zip");
    strcpy(rs.m_szFileSHA256, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    rs.m_fileID = 99;
    rs.m_pos = 4096;
    rs.m_szResult = _uploadfile_continue;

    STRU_UPLOADFILEINFORS rsBack;
    roundTripPacket(
        rs, static_cast<char>(_default_protocol_uploadfileinfo_rs),
        ProtocolFactory::serializeUploadFileInfoRS, ProtocolFactory::deserializeUploadFileInfoRS, &rsBack);
    QCOMPARE(string(rsBack.m_szFileName), string("archive.zip"));
    QCOMPARE(string(rsBack.m_szFileSHA256), string(rs.m_szFileSHA256));
    QCOMPARE(rsBack.m_fileID, static_cast<int64_t>(99));
    QCOMPARE(rsBack.m_pos, static_cast<int64_t>(4096));
    QCOMPARE(static_cast<int>(rsBack.m_szResult), _uploadfile_continue);
}

// ---------------------------------------------------------------------------
// 上传块 RQ/RS (10/11) — 4KB 内容
// ---------------------------------------------------------------------------
void TestProtocolFactory::uploadFileBlockRoundTrip()
{
    STRU_UPLOADFILEBLOCKRQ rq;
    memset(&rq, 0, sizeof(rq));
    rq.m_ntype = static_cast<char>(_default_protocol_uploadfileblock_rq);
    rq.m_userId = 3;
    rq.m_fileID = 99;
    rq.m_blockSeq = 5;
    rq.m_fileblocksize = MAXFILECONTENT;   // 满 4KB 块
    for (int i = 0; i < MAXFILECONTENT; ++i)
        rq.m_szFileContent[i] = static_cast<char>(i % 251);

    STRU_UPLOADFILEBLOCKRQ back;
    roundTripPacket(
        rq, static_cast<char>(_default_protocol_uploadfileblock_rq),
        ProtocolFactory::serializeUploadFileBlockRQ, ProtocolFactory::deserializeUploadFileBlockRQ, &back);
    QCOMPARE(back.m_userId, static_cast<int64_t>(3));
    QCOMPARE(back.m_fileID, static_cast<int64_t>(99));
    QCOMPARE(static_cast<int>(back.m_blockSeq), 5);
    QCOMPARE(static_cast<int>(back.m_fileblocksize), MAXFILECONTENT);
    QVERIFY(memcmp(back.m_szFileContent, rq.m_szFileContent, MAXFILECONTENT) == 0);

    // 零字节块
    STRU_UPLOADFILEBLOCKRQ rq0;
    memset(&rq0, 0, sizeof(rq0));
    rq0.m_ntype = static_cast<char>(_default_protocol_uploadfileblock_rq);
    rq0.m_fileblocksize = 0;
    STRU_UPLOADFILEBLOCKRQ back0;
    roundTripPacket(
        rq0, static_cast<char>(_default_protocol_uploadfileblock_rq),
        ProtocolFactory::serializeUploadFileBlockRQ, ProtocolFactory::deserializeUploadFileBlockRQ, &back0);
    QCOMPARE(static_cast<int>(back0.m_fileblocksize), 0);
}

// ---------------------------------------------------------------------------
// 稀疏指纹预检 RQ/RS (37/38)
// ---------------------------------------------------------------------------
void TestProtocolFactory::sparseCheckRoundTrip()
{
    STRU_SPARSECHECKRQ rq;
    memset(&rq, 0, sizeof(rq));
    rq.m_ntype = static_cast<char>(_default_protocol_sparsecheck_rq);
    rq.m_userId = 1;
    rq.m_fileSize = 8388608;
    strcpy(rq.m_szSparseFingerprint, "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2");

    STRU_SPARSECHECKRQ back;
    roundTripPacket(
        rq, static_cast<char>(_default_protocol_sparsecheck_rq),
        ProtocolFactory::serializeSparseCheckRQ, ProtocolFactory::deserializeSparseCheckRQ, &back);
    QCOMPARE(back.m_userId, static_cast<int64_t>(1));
    QCOMPARE(back.m_fileSize, static_cast<int64_t>(8388608));
    QCOMPARE(string(back.m_szSparseFingerprint), string(rq.m_szSparseFingerprint));

    STRU_SPARSECHECKRS rs;
    rs.m_ntype = static_cast<char>(_default_protocol_sparsecheck_rs);
    rs.m_szResult = 1;   // 可能存在 (L2/L3 秒传预检语义)
    STRU_SPARSECHECKRS rsBack;
    roundTripPacket(
        rs, static_cast<char>(_default_protocol_sparsecheck_rs),
        ProtocolFactory::serializeSparseCheckRS, ProtocolFactory::deserializeSparseCheckRS, &rsBack);
    QCOMPARE(static_cast<int>(rsBack.m_szResult), 1);
}

// ---------------------------------------------------------------------------
// 分享撤销 RQ/RS (35/36)
// ---------------------------------------------------------------------------
void TestProtocolFactory::deleteShareRoundTrip()
{
    STRU_DELETESHARERQ rq;
    rq.m_ntype = static_cast<char>(_default_protocol_deleteshare_rq);
    rq.m_userId = 9;
    rq.m_fileID = 77;
    STRU_DELETESHARERQ back;
    roundTripPacket(
        rq, static_cast<char>(_default_protocol_deleteshare_rq),
        ProtocolFactory::serializeDeleteShareRQ, ProtocolFactory::deserializeDeleteShareRQ, &back);
    QCOMPARE(back.m_userId, static_cast<int64_t>(9));
    QCOMPARE(back.m_fileID, static_cast<int64_t>(77));

    STRU_DELETESHARERS rs;
    rs.m_ntype = static_cast<char>(_default_protocol_deleteshare_rs);
    rs.m_fileID = 77;
    rs.m_szResult = 1;
    STRU_DELETESHARERS rsBack;
    roundTripPacket(
        rs, static_cast<char>(_default_protocol_deleteshare_rs),
        ProtocolFactory::serializeDeleteShareRS, ProtocolFactory::deserializeDeleteShareRS, &rsBack);
    QCOMPARE(rsBack.m_fileID, static_cast<int64_t>(77));
    QCOMPARE(static_cast<int>(rsBack.m_szResult), 1);
}

// ---------------------------------------------------------------------------
// AI 预览 RQ/RS (24/25) — 大字段
// ---------------------------------------------------------------------------
void TestProtocolFactory::aiPreviewRoundTrip()
{
    STRU_AIPREVIEWRQ rq;
    rq.m_ntype = static_cast<char>(_default_protocol_aipreview_rq);
    rq.m_userId = 2;
    rq.m_fileID = 31;
    STRU_AIPREVIEWRQ back;
    roundTripPacket(
        rq, static_cast<char>(_default_protocol_aipreview_rq),
        ProtocolFactory::serializeAIPreviewRQ, ProtocolFactory::deserializeAIPreviewRQ, &back);
    QCOMPARE(back.m_userId, static_cast<int64_t>(2));
    QCOMPARE(back.m_fileID, static_cast<int64_t>(31));

    STRU_AIPREVIEWRS rs;
    memset(&rs, 0, sizeof(rs));
    rs.m_ntype = static_cast<char>(_default_protocol_aipreview_rs);
    rs.m_fileID = 31;
    strcpy(rs.m_szSummary, "这是AI生成的100字中文摘要，覆盖文件核心内容与结论。");
    strcpy(rs.m_szKeywords, "Qt, IOCP, SHA-256, BinaryStream, 秒传");
    strcpy(rs.m_szKeySentences, "信号槽是Qt的核心通信机制。IOCP使用内核级异步I/O。");
    strcpy(rs.m_szFileType, "C++ 源码");
    strcpy(rs.m_szFileName, "tcpkernel.cpp");
    rs.m_nRawContentLen = 8192;
    for (int i = 0; i < MAXFILECONTENT * 2; ++i)
        rs.m_szRawContent[i] = static_cast<char>('a' + (i % 26));
    strcpy(rs.m_szAIError, "");
    rs.m_szResult = 0;

    STRU_AIPREVIEWRS rsBack;
    roundTripPacket(
        rs, static_cast<char>(_default_protocol_aipreview_rs),
        ProtocolFactory::serializeAIPreviewRS, ProtocolFactory::deserializeAIPreviewRS, &rsBack);
    QCOMPARE(rsBack.m_fileID, static_cast<int64_t>(31));
    QCOMPARE(string(rsBack.m_szSummary), string(rs.m_szSummary));
    QCOMPARE(string(rsBack.m_szKeywords), string(rs.m_szKeywords));
    QCOMPARE(string(rsBack.m_szKeySentences), string(rs.m_szKeySentences));
    QCOMPARE(string(rsBack.m_szFileType), string(rs.m_szFileType));
    QCOMPARE(string(rsBack.m_szFileName), string(rs.m_szFileName));
    QCOMPARE(static_cast<int>(rsBack.m_nRawContentLen), MAXFILECONTENT * 2);
    QVERIFY(memcmp(rsBack.m_szRawContent, rs.m_szRawContent, MAXFILECONTENT * 2) == 0);
    QCOMPARE(string(rsBack.m_szAIError), string(""));
    QCOMPARE(static_cast<int>(rsBack.m_szResult), 0);
}

// ---------------------------------------------------------------------------
// AI 搜索 RQ/RS (26/27) — 多结果
// ---------------------------------------------------------------------------
void TestProtocolFactory::aiSearchRoundTrip()
{
    STRU_AISEARCHRQ rq;
    rq.m_ntype = static_cast<char>(_default_protocol_aisearch_rq);
    rq.m_userId = 2;
    strcpy(rq.m_szQuery, "那个关于信号槽的文档");
    STRU_AISEARCHRQ back;
    roundTripPacket(
        rq, static_cast<char>(_default_protocol_aisearch_rq),
        ProtocolFactory::serializeAISearchRQ, ProtocolFactory::deserializeAISearchRQ, &back);
    QCOMPARE(back.m_userId, static_cast<int64_t>(2));
    QCOMPARE(string(back.m_szQuery), string("那个关于信号槽的文档"));

    STRU_AISEARCHRS rs;
    memset(&rs, 0, sizeof(rs));
    rs.m_ntype = static_cast<char>(_default_protocol_aisearch_rs);
    rs.m_nResultNum = 2;
    strcpy(rs.m_aryResults[0].m_fileInfo.m_szFileName, "qt-signals.md");
    rs.m_aryResults[0].m_fileInfo.m_filesize = 5000;
    strcpy(rs.m_aryResults[0].m_fileInfo.m_szFileUploadTime, "2026-08-01 08:00:00");
    rs.m_aryResults[0].m_fileInfo.m_fileID = 5;
    strcpy(rs.m_aryResults[0].m_szMatchReason, "内容与「Qt 信号槽机制」高度相关 (相似度 0.87)");
    strcpy(rs.m_aryResults[0].m_szFileSHA256, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    strcpy(rs.m_aryResults[1].m_fileInfo.m_szFileName, "notes.txt");
    rs.m_aryResults[1].m_fileInfo.m_filesize = 800;
    strcpy(rs.m_aryResults[1].m_fileInfo.m_szFileUploadTime, "2026-07-30 20:00:00");
    rs.m_aryResults[1].m_fileInfo.m_fileID = 6;
    strcpy(rs.m_aryResults[1].m_szMatchReason, "文件名匹配 (包含 'Qt')");
    strcpy(rs.m_aryResults[1].m_szFileSHA256, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    rs.m_szResult = 0;

    STRU_AISEARCHRS rsBack;
    roundTripPacket(
        rs, static_cast<char>(_default_protocol_aisearch_rs),
        ProtocolFactory::serializeAISearchRS, ProtocolFactory::deserializeAISearchRS, &rsBack);
    QCOMPARE(rsBack.m_nResultNum, 2);
    QCOMPARE(string(rsBack.m_aryResults[0].m_fileInfo.m_szFileName), string("qt-signals.md"));
    QCOMPARE(string(rsBack.m_aryResults[0].m_szMatchReason), string(rs.m_aryResults[0].m_szMatchReason));
    QCOMPARE(string(rsBack.m_aryResults[0].m_szFileSHA256), string(rs.m_aryResults[0].m_szFileSHA256));
    QCOMPARE(string(rsBack.m_aryResults[1].m_fileInfo.m_szFileName), string("notes.txt"));
    QCOMPARE(rsBack.m_aryResults[1].m_fileInfo.m_fileID, static_cast<int64_t>(6));
    QCOMPARE(static_cast<int>(rsBack.m_szResult), 0);
}

// ---------------------------------------------------------------------------
// AI 标签 RQ/RS (28/29) — 标签数组
// ---------------------------------------------------------------------------
void TestProtocolFactory::aiTagRoundTrip()
{
    STRU_AITAGRQ rq;
    rq.m_ntype = static_cast<char>(_default_protocol_aitag_rq);
    rq.m_userId = 2;
    rq.m_fileID = 31;
    STRU_AITAGRQ back;
    roundTripPacket(
        rq, static_cast<char>(_default_protocol_aitag_rq),
        ProtocolFactory::serializeAITagRQ, ProtocolFactory::deserializeAITagRQ, &back);
    QCOMPARE(back.m_userId, static_cast<int64_t>(2));
    QCOMPARE(back.m_fileID, static_cast<int64_t>(31));

    STRU_AITAGRS rs;
    memset(&rs, 0, sizeof(rs));
    rs.m_ntype = static_cast<char>(_default_protocol_aitag_rs);
    rs.m_fileID = 31;
    rs.m_nTagNum = 3;
    strcpy(rs.m_szTags[0], "技术文档");
    strcpy(rs.m_szTags[1], "项目代码");
    strcpy(rs.m_szTags[2], "C++");
    rs.m_nNewTagSuggestions = 2;
    strcpy(rs.m_szNewTags[0], "IOCP");
    strcpy(rs.m_szNewTags[1], "网络编程");
    rs.m_szResult = 0;

    STRU_AITAGRS rsBack;
    roundTripPacket(
        rs, static_cast<char>(_default_protocol_aitag_rs),
        ProtocolFactory::serializeAITagRS, ProtocolFactory::deserializeAITagRS, &rsBack);
    QCOMPARE(rsBack.m_fileID, static_cast<int64_t>(31));
    QCOMPARE(rsBack.m_nTagNum, 3);
    QCOMPARE(string(rsBack.m_szTags[0]), string("技术文档"));
    QCOMPARE(string(rsBack.m_szTags[1]), string("项目代码"));
    QCOMPARE(string(rsBack.m_szTags[2]), string("C++"));
    QCOMPARE(rsBack.m_nNewTagSuggestions, 2);
    QCOMPARE(string(rsBack.m_szNewTags[0]), string("IOCP"));
    QCOMPARE(string(rsBack.m_szNewTags[1]), string("网络编程"));
    QCOMPARE(static_cast<int>(rsBack.m_szResult), 0);
}

// ---------------------------------------------------------------------------
// 流媒体 Token RQ/RS (33/34)
// ---------------------------------------------------------------------------
void TestProtocolFactory::streamTokenRoundTrip()
{
    STRU_STREAMTOKENRQ rq;
    rq.m_ntype = static_cast<char>(_default_protocol_streamtoken_rq);
    rq.m_userId = 2;
    rq.m_fileID = 31;
    STRU_STREAMTOKENRQ back;
    roundTripPacket(
        rq, static_cast<char>(_default_protocol_streamtoken_rq),
        ProtocolFactory::serializeStreamTokenRQ, ProtocolFactory::deserializeStreamTokenRQ, &back);
    QCOMPARE(back.m_userId, static_cast<int64_t>(2));
    QCOMPARE(back.m_fileID, static_cast<int64_t>(31));

    STRU_STREAMTOKENRS rs;
    memset(&rs, 0, sizeof(rs));
    rs.m_ntype = static_cast<char>(_default_protocol_streamtoken_rs);
    rs.m_fileID = 31;
    strcpy(rs.m_szToken, "d41d8cd98f00b204e9800998ecf8427e");
    rs.m_nTimestamp = 1784600000;
    rs.m_nHttpPort = 8900;
    rs.m_fileSize = 123456789;
    STRU_STREAMTOKENRS rsBack;
    roundTripPacket(
        rs, static_cast<char>(_default_protocol_streamtoken_rs),
        ProtocolFactory::serializeStreamTokenRS, ProtocolFactory::deserializeStreamTokenRS, &rsBack);
    QCOMPARE(rsBack.m_fileID, static_cast<int64_t>(31));
    QCOMPARE(string(rsBack.m_szToken), string(rs.m_szToken));
    QCOMPARE(rsBack.m_nTimestamp, static_cast<int64_t>(1784600000));
    QCOMPARE(static_cast<int>(rsBack.m_nHttpPort), 8900);
    QCOMPARE(rsBack.m_fileSize, static_cast<int64_t>(123456789));
}

// ---------------------------------------------------------------------------
// 分布式: 复制 RQ/RS (30/31) + 重定向 RS (32)
// ---------------------------------------------------------------------------
void TestProtocolFactory::clusterRoundTrip()
{
    STRU_REPLICATEBLOCKRQ rq;
    rq.m_ntype = static_cast<char>(_default_protocol_replicateblock_rq);
    rq.m_fileId = 100;
    rq.m_blockSeq = 3;
    rq.m_offset = 40960;
    rq.m_dataLen = 4096;
    for (int i = 0; i < MAXFILECONTENT; ++i)
        rq.m_szData[i] = static_cast<char>(i);
    STRU_REPLICATEBLOCKRQ back;
    roundTripPacket(
        rq, static_cast<char>(_default_protocol_replicateblock_rq),
        ProtocolFactory::serializeReplicateBlockRQ, ProtocolFactory::deserializeReplicateBlockRQ, &back);
    QCOMPARE(back.m_fileId, static_cast<int64_t>(100));
    QCOMPARE(static_cast<int>(back.m_blockSeq), 3);
    QCOMPARE(back.m_offset, static_cast<int64_t>(40960));
    QCOMPARE(back.m_dataLen, static_cast<int64_t>(4096));
    QVERIFY(memcmp(back.m_szData, rq.m_szData, MAXFILECONTENT) == 0);

    STRU_REPLICATEBLOCKRS rs;
    rs.m_ntype = static_cast<char>(_default_protocol_replicateblock_rs);
    rs.m_fileId = 100;
    rs.m_blockSeq = 3;
    rs.m_szResult = 1;
    STRU_REPLICATEBLOCKRS rsBack;
    roundTripPacket(
        rs, static_cast<char>(_default_protocol_replicateblock_rs),
        ProtocolFactory::serializeReplicateBlockRS, ProtocolFactory::deserializeReplicateBlockRS, &rsBack);
    QCOMPARE(rsBack.m_fileId, static_cast<int64_t>(100));
    QCOMPARE(static_cast<int>(rsBack.m_blockSeq), 3);
    QCOMPARE(static_cast<int>(rsBack.m_szResult), 1);

    STRU_REDIRECTRS redirect;
    redirect.m_ntype = static_cast<char>(_default_protocol_redirect_rs);
    strcpy(redirect.m_szRedirectIP, "192.168.1.10");
    redirect.m_nRedirectPort = 8898;
    redirect.m_szResult = _redirect_permanent;
    STRU_REDIRECTRS redirectBack;
    roundTripPacket(
        redirect, static_cast<char>(_default_protocol_redirect_rs),
        ProtocolFactory::serializeRedirectRS, ProtocolFactory::deserializeRedirectRS, &redirectBack);
    QCOMPARE(string(redirectBack.m_szRedirectIP), string("192.168.1.10"));
    QCOMPARE(static_cast<int>(redirectBack.m_nRedirectPort), 8898);
    QCOMPARE(static_cast<int>(redirectBack.m_szResult), _redirect_permanent);
}
