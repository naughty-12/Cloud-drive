#ifndef TST_BINARYSTREAM_H
#define TST_BINARYSTREAM_H

#include <QObject>

/**
 * BinaryStream 二进制读写器测试。
 * 覆盖: 整数/字符串/定长字符串/原始字节的写入与读取、
 * 网络字节序、边界检查 (越界抛异常)、状态管理。
 */
class TestBinaryStream : public QObject
{
    Q_OBJECT
private slots:
    void int32RoundTrip();          // int32 往返
    void int64RoundTrip();          // int64 往返
    void stringRoundTrip();         // 变长字符串往返 (空串/中文/长串)
    void fixedStringRoundTrip();    // 定长字符串往返 (补零/截断/恰满)
    void rawRoundTrip();            // 原始字节往返 (含 NUL 的二进制)
    void mixedFieldRoundTrip();     // 混合字段单流往返
    void bigEndianWireFormat();     // 网络字节序 (大端) 字节级验证
    void fromDataReads();           // fromData 构造读取流
    void overflowThrows();          // 越界读取必须抛 std::runtime_error
    void resetAndRemaining();       // reset / remaining / readPos 语义
};

#endif // TST_BINARYSTREAM_H
