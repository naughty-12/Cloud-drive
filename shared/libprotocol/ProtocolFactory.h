#ifndef PROTOCOLFACTORY_H
#define PROTOCOLFACTORY_H

/**
 * @file ProtocolFactory.h
 * @brief 全部协议包的序列化 / 反序列化工厂。
 *
 * ProtocolFactory 桥接旧的固定结构体 Packdef.h 类型与新的 BinaryStream
 * 二进制读写器，提供：
 *
 *   - parseType():    从原始包体中提取 m_ntype。
 *   - serialize*():   将结构体转换为线格式字节向量。
 *   - deserialize*(): 将原始包体解析为结构体
 *                     (BinaryStream::fromData → 提取字段)。
 *
 * ⚠️ 线格式说明（与实现保持一致，2026-08-20 修订）：
 *   serialize*() 的产物 = [1 字节 m_ntype][BinaryStream payload]，
 *   **不包含 4 字节长度前缀**。加帧（[4 字节大端总长度][body]）由网络层
 *   (INet::sendData / IocpServer / TCPClient) 完成；如需显式加帧，
 *   调用 wrapPacket()。参见 ProtocolFactory.cpp 顶部注释与
 *   tests/unit_tests/tst_protocolfactory.cpp 的往返测试。
 */

#include "Packdef.h"
#include "BinaryStream.h"

#include <cstdint>
#include <vector>

class ProtocolFactory {
public:
    // -----------------------------------------------------------------------
    // 包类型识别
    // -----------------------------------------------------------------------

    /**
     * @brief 从原始包体中提取协议类型（m_ntype）。
     *
     * 包体即 4 字节长度前缀之后的有效载荷。
     * 每个包体的第一个字节就是 m_ntype 字段。
     *
     * @param packetBody 指向包体起始位置的指针。
     * @param bodyLen    包体长度（字节），必须 >= 1。
     * @return m_ntype 值（有效协议为 2-29）。
     * @throws 若 bodyLen < 1 则抛出 std::runtime_error。
     */
    static char parseType(const char* packetBody, int bodyLen);

    // -----------------------------------------------------------------------
    // 包封装
    // -----------------------------------------------------------------------

    /**
     * @brief 为 BinaryStream 包体添加 4 字节大端长度前缀。
     *
     * 生成完整的线格式数据包：
     *   [4 字节：bodyLen（大端，32 位）][包体字节]
     *
     * @param body 已序列化的包体载荷。
     * @return 完整的线格式数据包。
     */
    static std::vector<uint8_t> wrapPacket(const std::vector<uint8_t>& body);

    // =======================================================================
    // 序列化方法（结构体 → std::vector<uint8_t> 线格式）
    // =======================================================================

    // -- 注册 --
    static std::vector<uint8_t> serializeRegisterRQ(const STRU_REGISTERRQ& s);
    static std::vector<uint8_t> serializeRegisterRS(const STRU_REGISTERRS& s);

    // -- 登录 --
    static std::vector<uint8_t> serializeLoginRQ(const STRU_LOGINRQ& s);
    static std::vector<uint8_t> serializeLoginRS(const STRU_LOGINRS& s);

    // -- 获取文件列表 --
    static std::vector<uint8_t> serializeGetFileListRQ(const STRU_GETFILELISTRQ& s);
    static std::vector<uint8_t> serializeGetFileListRS(const STRU_GETFILELISTRS& s);

    // -- 上传文件信息 --
    static std::vector<uint8_t> serializeUploadFileInfoRQ(const STRU_UPLOADFILEINFORQ& s);
    static std::vector<uint8_t> serializeUploadFileInfoRS(const STRU_UPLOADFILEINFORS& s);

    // -- 上传文件块 --
    static std::vector<uint8_t> serializeUploadFileBlockRQ(const STRU_UPLOADFILEBLOCKRQ& s);
    static std::vector<uint8_t> serializeUploadFileBlockRS(const STRU_UPLOADFILEBLOCKRS& s);

    // -- 下载文件信息 --
    static std::vector<uint8_t> serializeDownloadFileInfoRQ(const STRU_DOWNLOADFILEINFORQ& s);
    static std::vector<uint8_t> serializeDownloadFileInfoRS(const STRU_DOWNLOADFILEINFORS& s);

    // -- 下载文件块 --
    static std::vector<uint8_t> serializeDownloadFileBlockRQ(const STRU_DOWNLOADFILEBLOCKRQ& s);
    static std::vector<uint8_t> serializeDownloadFileBlockRS(const STRU_DOWNLOADFILEBLOCKRS& s);

    // -- 搜索文件 --
    static std::vector<uint8_t> serializeSearchFileRQ(const STRU_SEARCHFILERQ& s);
    static std::vector<uint8_t> serializeSearchFileRS(const STRU_SEARCHFILERS& s);

    // -- 删除文件 --
    static std::vector<uint8_t> serializeDeleteFileRQ(const STRU_DELETEFILERQ& s);
    static std::vector<uint8_t> serializeDeleteFileRS(const STRU_DELETEFILERS& s);

    // -- 分享文件 --
    static std::vector<uint8_t> serializeShareFileRQ(const STRU_SHAREFILERQ& s);
    static std::vector<uint8_t> serializeShareFileRS(const STRU_SHAREFILERS& s);

    // -- 撤销分享 (F10-4) --
    static std::vector<uint8_t> serializeDeleteShareRQ(const STRU_DELETESHARERQ& s);
    static std::vector<uint8_t> serializeDeleteShareRS(const STRU_DELETESHARERS& s);

    // -- 稀疏指纹预检（秒传 L2 漏斗）--
    static std::vector<uint8_t> serializeSparseCheckRQ(const STRU_SPARSECHECKRQ& s);
    static std::vector<uint8_t> serializeSparseCheckRS(const STRU_SPARSECHECKRS& s);

    // -- 获取文件（提取码）--
    static std::vector<uint8_t> serializeGetFileRQ(const STRU_GETFILERQ& s);
    static std::vector<uint8_t> serializeGetFileRS(const STRU_GETFILERS& s);

    // -- AI 预览（Phase 3）--
    static std::vector<uint8_t> serializeAIPreviewRQ(const STRU_AIPREVIEWRQ& s);
    static std::vector<uint8_t> serializeAIPreviewRS(const STRU_AIPREVIEWRS& s);

    // -- AI 语义搜索（Phase 3）--
    static std::vector<uint8_t> serializeAISearchRQ(const STRU_AISEARCHRQ& s);
    static std::vector<uint8_t> serializeAISearchRS(const STRU_AISEARCHRS& s);

    // -- AI 自动标签（Phase 3）--
    static std::vector<uint8_t> serializeAITagRQ(const STRU_AITAGRQ& s);
    static std::vector<uint8_t> serializeAITagRS(const STRU_AITAGRS& s);

    // -- 流媒体 Token（Phase 2：HTTP 流媒体）--
    static std::vector<uint8_t> serializeStreamTokenRQ(const STRU_STREAMTOKENRQ& s);
    static std::vector<uint8_t> serializeStreamTokenRS(const STRU_STREAMTOKENRS& s);

    // -- 集群：复制块（分布式 L2）--
    static std::vector<uint8_t> serializeReplicateBlockRQ(const STRU_REPLICATEBLOCKRQ& s);
    static std::vector<uint8_t> serializeReplicateBlockRS(const STRU_REPLICATEBLOCKRS& s);

    // -- 集群：重定向（分布式 L2）--
    static std::vector<uint8_t> serializeRedirectRS(const STRU_REDIRECTRS& s);

    // =======================================================================
    // 反序列化方法（原始包体 → 结构体）
    // 所有 RQ 类型 —— RS 反序列化在客户端侧完成
    // =======================================================================

    /**
     * @brief 将原始包体反序列化为结构体。
     *
     * 包体是 4 字节长度前缀之后、且已消费 m_ntype 字节
     * 之后的载荷（m_ntype 在调用 deserialize 之前由
     * parseType() 单独读取）。
     *
     * @param body 指向 m_ntype 字节之后的包体。
     * @param len  剩余包体的长度。
     * @return 反序列化得到的结构体。
     */
    static STRU_REGISTERRQ          deserializeRegisterRQ(const char* body, int len);
    static STRU_LOGINRQ             deserializeLoginRQ(const char* body, int len);
    static STRU_GETFILELISTRQ       deserializeGetFileListRQ(const char* body, int len);
    static STRU_UPLOADFILEINFORQ    deserializeUploadFileInfoRQ(const char* body, int len);
    static STRU_UPLOADFILEBLOCKRQ   deserializeUploadFileBlockRQ(const char* body, int len);
    static STRU_DOWNLOADFILEINFORQ  deserializeDownloadFileInfoRQ(const char* body, int len);
    static STRU_DOWNLOADFILEBLOCKRQ deserializeDownloadFileBlockRQ(const char* body, int len);
    static STRU_SEARCHFILERQ        deserializeSearchFileRQ(const char* body, int len);
    static STRU_DELETEFILERQ        deserializeDeleteFileRQ(const char* body, int len);
    static STRU_SHAREFILERQ         deserializeShareFileRQ(const char* body, int len);
    static STRU_DELETESHARERQ       deserializeDeleteShareRQ(const char* body, int len);
    static STRU_SPARSECHECKRQ       deserializeSparseCheckRQ(const char* body, int len);
    static STRU_GETFILERQ           deserializeGetFileRQ(const char* body, int len);
    static STRU_AIPREVIEWRQ         deserializeAIPreviewRQ(const char* body, int len);
    static STRU_AISEARCHRQ          deserializeAISearchRQ(const char* body, int len);
    static STRU_AITAGRQ             deserializeAITagRQ(const char* body, int len);

    // 集群：复制块 + 重定向
    static STRU_REPLICATEBLOCKRQ   deserializeReplicateBlockRQ(const char* body, int len);
    static STRU_REPLICATEBLOCKRS   deserializeReplicateBlockRS(const char* body, int len);
    static STRU_REDIRECTRS         deserializeRedirectRS(const char* body, int len);

    // 流媒体 Token（Phase 2：HTTP 流媒体）
    static STRU_STREAMTOKENRQ      deserializeStreamTokenRQ(const char* body, int len);
    static STRU_STREAMTOKENRS      deserializeStreamTokenRS(const char* body, int len);

    // RS 类型反序列化（客户端侧）
    static STRU_REGISTERRS         deserializeRegisterRS(const char* body, int len);
    static STRU_LOGINRS            deserializeLoginRS(const char* body, int len);
    static STRU_GETFILELISTRS      deserializeGetFileListRS(const char* body, int len);
    static STRU_UPLOADFILEINFORS   deserializeUploadFileInfoRS(const char* body, int len);
    static STRU_UPLOADFILEBLOCKRS  deserializeUploadFileBlockRS(const char* body, int len);
    static STRU_DOWNLOADFILEINFORS deserializeDownloadFileInfoRS(const char* body, int len);
    static STRU_DOWNLOADFILEBLOCKRS deserializeDownloadFileBlockRS(const char* body, int len);
    static STRU_SEARCHFILERS       deserializeSearchFileRS(const char* body, int len);
    static STRU_DELETEFILERS       deserializeDeleteFileRS(const char* body, int len);
    static STRU_SHAREFILERS        deserializeShareFileRS(const char* body, int len);
    static STRU_DELETESHARERS      deserializeDeleteShareRS(const char* body, int len);
    static STRU_SPARSECHECKRS      deserializeSparseCheckRS(const char* body, int len);
    static STRU_GETFILERS          deserializeGetFileRS(const char* body, int len);
    static STRU_AIPREVIEWRS        deserializeAIPreviewRS(const char* body, int len);
    static STRU_AISEARCHRS         deserializeAISearchRS(const char* body, int len);
    static STRU_AITAGRS            deserializeAITagRS(const char* body, int len);

private:
    /// 辅助函数：将 FILEINFO 序列化到 BinaryStream。
    static void serializeFileInfo(BinaryStream& bs, const FILEINFO& info);

    /// 辅助函数：从 BinaryStream 反序列化 FILEINFO。
    static FILEINFO deserializeFileInfo(BinaryStream& bs);
};

#endif // PROTOCOLFACTORY_H
