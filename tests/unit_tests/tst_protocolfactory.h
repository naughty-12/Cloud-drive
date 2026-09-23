#ifndef TST_PROTOCOLFACTORY_H
#define TST_PROTOCOLFACTORY_H

#include <QObject>

/**
 * ProtocolFactory 协议工厂测试。
 * 覆盖: parseType / wrapPacket 基础设施,
 * 以及全部 RQ/RS 协议类型的 序列化→反序列化 往返一致性
 * (与 plans.md Phase 1 验收标准"BinaryStream 循环一致性测试"对应)。
 */
class TestProtocolFactory : public QObject
{
    Q_OBJECT
private slots:
    void parseTypeBasics();            // 类型字节提取 + 非法输入抛异常
    void wrapPacketBasics();           // 4 字节大端长度前缀 + body
    void registerRoundTrip();          // 注册 RQ/RS (2/3)
    void loginRoundTrip();             // 登录 RQ/RS (4/5)
    void getFileListRoundTrip();       // 文件列表 RQ/RS (6/7) — 多 FILEINFO
    void uploadFileInfoRoundTrip();    // 上传信息 RQ/RS (8/9) — FILEINFO + SHA-256
    void uploadFileBlockRoundTrip();   // 上传块 RQ/RS (10/11) — 4KB 内容
    void sparseCheckRoundTrip();       // 稀疏指纹预检 RQ/RS (37/38)
    void deleteShareRoundTrip();       // 分享撤销 RQ/RS (35/36)
    void aiPreviewRoundTrip();         // AI 预览 RQ/RS (24/25) — 大字段
    void aiSearchRoundTrip();          // AI 搜索 RQ/RS (26/27) — 多结果
    void aiTagRoundTrip();             // AI 标签 RQ/RS (28/29) — 标签数组
    void streamTokenRoundTrip();       // 流媒体 Token RQ/RS (33/34)
    void clusterRoundTrip();           // 复制 RQ/RS (30/31) + 重定向 RS (32)
};

#endif // TST_PROTOCOLFACTORY_H
