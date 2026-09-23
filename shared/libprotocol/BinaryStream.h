#ifndef BINARYSTREAM_H
#define BINARYSTREAM_H

/**
 * @file BinaryStream.h
 * @brief 面向网络协议的超轻量二进制读写器。
 *
 * BinaryStream 提供简单、类型安全的二进制协议报文序列化/反序列化能力。
 * 所有多字节整数均以网络字节序（大端）存储，字节序转换使用手写位运算完成
 * （零系统调用依赖；实现见 BinaryStream.cpp 中的匿名命名空间）。
 *
 * 设计目标：
 *   - 零外部依赖（仅 <cstdint>、<string>、<vector>、<cstring>）
 *   - RAII 安全的缓冲区管理（std::vector<uint8_t>）
 *   - 带边界检查的读取（溢出时抛出 std::runtime_error）
 *   - 兼容现有 [4 字节长度][payload] 线上报文格式
 *
 * 用法示例：
 * @code
 *   // 写入
 *   BinaryStream bs;
 *   bs << (int32_t)42 << std::string("hello");
 *   auto packet = ProtocolFactory::wrapPacket(bs.data());
 *   send(sock, packet.data(), packet.size());
 *
 *   // 读取
 *   BinaryStream bs2 = BinaryStream::fromData(buf, len);
 *   int32_t val; std::string str;
 *   bs2 >> val >> str;
 * @endcode
 */

#include <cstdint>
#include <string>
#include <vector>
#include <cstring>
#include <stdexcept>

// 为 Packdef.h 的 MAXSIZE 常量提供前置声明
// （BinaryStream 不依赖 Packdef.h；定长字符串的读写方法
// 以参数形式接收长度。）
#ifndef MAXSIZE
#define BINSTREAM_DEFAULT_FIXED_STR 45
#endif

class BinaryStream {
public:
    // -----------------------------------------------------------------------
    // 构造
    // -----------------------------------------------------------------------

    /// 构造一个空的、用于写入的流。
    BinaryStream();

    /// 从原始缓冲区数据构造一个用于读取的流。
    /// @param data 指向原始字节缓冲区的指针（不拥有所有权）。
    /// @param len  要拷贝进内部缓冲区的字节数。
    static BinaryStream fromData(const char* data, int len);

    // -----------------------------------------------------------------------
    // 写操作（序列化 → 网络字节序）
    // -----------------------------------------------------------------------

    /// 写入 32 位有符号整数（手写字节交换 → 4 字节大端）。
    /// @param val 要写入的值。
    /// @return 返回本流的引用，便于链式调用。
    BinaryStream& operator<<(int32_t val);

    /// 写入 64 位有符号整数（手写大端：高/低 32 位交换，无需 htonll）。
    /// @param val 要写入的值。
    /// @return 返回本流的引用，便于链式调用。
    BinaryStream& operator<<(int64_t val);

    /// 写入变长字符串（2 字节长度前缀 + UTF-8 内容）。
    /// 长度前缀使用手写 16 位字节交换（网络字节序）。
    /// @param str 要写入的字符串。最大长度为 65535 字节。
    /// @return 返回本流的引用，便于链式调用。
    BinaryStream& operator<<(const std::string& str);

    /// 写入定长 char 数组（恰好 @p fixedSize 字节）。
    /// 若源字符串较短，剩余部分以 0 填充。
    /// 用于兼容旧的 MAXSIZE（45 字节）字符串字段。
    /// @param str 要写入的以 '\0' 结尾的 C 字符串。
    /// @param fixedSize 要写入的字节数（默认：Packdef.h 中的 MAXSIZE，或 45）。
    /// @return 返回本流的引用，便于链式调用。
    /// @note 这是命名方法而非 operator<<，因为定长必须显式指定才能保证正确性。
    BinaryStream& writeFixedString(const char* str, int fixedSize);

    /// 将原始字节直接写入缓冲区（无长度前缀、无字节序转换）。
    /// @param data 指向原始字节的指针。
    /// @param len  要写入的字节数。
    void writeRaw(const char* data, int len);

    // -----------------------------------------------------------------------
    // 读操作（反序列化 → 主机字节序）
    // -----------------------------------------------------------------------

    /// 读取 32 位有符号整数（4 字节 → ntohl → 主机字节序）。
    /// @param val [out] 读出的值。
    /// @return 返回本流的引用，便于链式调用。
    /// @throws std::runtime_error 若剩余字节不足 4 字节。
    BinaryStream& operator>>(int32_t& val);

    /// 读取 64 位有符号整数（8 字节 → 手写 ntohll → 主机字节序）。
    /// @param val [out] 读出的值。
    /// @return 返回本流的引用，便于链式调用。
    /// @throws std::runtime_error 若剩余字节不足 8 字节。
    BinaryStream& operator>>(int64_t& val);

    /// 读取变长字符串（2 字节长度前缀 → ntohs → 读取内容）。
    /// @param str [out] 读出的字符串。
    /// @return 返回本流的引用，便于链式调用。
    /// @throws std::runtime_error 若声明的长度超过剩余字节数。
    BinaryStream& operator>>(std::string& str);

    /// 从流中读取恰好 @p fixedSize 字节到 char 缓冲区。
    /// @param str [out] 目标缓冲区（至少 @p fixedSize 字节）。
    /// @param fixedSize 要读取的字节数。
    /// @return 返回本流的引用，便于链式调用。
    /// @throws std::runtime_error 若剩余字节不足 @p fixedSize 字节。
    /// @note 为清晰起见采用命名方法 —— 定长是显式指定的。
    BinaryStream& readFixedString(char* str, int fixedSize);

    /// 从缓冲区读取原始字节。
    /// @param data [out] 目标缓冲区。
    /// @param len  要读取的字节数。
    /// @throws std::runtime_error 若剩余字节不足 @p len 字节。
    void readRaw(char* data, int len);

    // -----------------------------------------------------------------------
    // 缓冲区访问
    // -----------------------------------------------------------------------

    /// 获取内部缓冲区（const 引用，避免拷贝开销）。
    /// @return 完整的序列化字节向量。
    const std::vector<uint8_t>& buffer() const { return m_buffer; }

    /// 获取内部缓冲区的一份拷贝。
    /// @return 序列化字节向量的副本。
    std::vector<uint8_t> data() const { return m_buffer; }

    /// 获取指向原始缓冲区数据的 const 指针。
    /// @return 指向首字节的指针；缓冲区为空时返回 nullptr。
    const uint8_t* dataPtr() const { return m_buffer.data(); }

    /// 获取缓冲区总大小（字节数）。
    /// @return 缓冲区中的字节数。
    int size() const { return static_cast<int>(m_buffer.size()); }

    /// 获取当前读取位置。
    /// @return 下一次读取的字节偏移量。
    int readPos() const { return m_readPos; }

    /// 获取剩余未读字节数。
    /// @return size() - readPos()。
    int remaining() const { return static_cast<int>(m_buffer.size()) - m_readPos; }

    // -----------------------------------------------------------------------
    // 状态管理
    // -----------------------------------------------------------------------

    /// 将流重置为空（清空缓冲区并复位读取位置）。
    void reset();

private:
    std::vector<uint8_t> m_buffer;   ///< 内部字节存储
    int                  m_readPos;  ///< 当前读取游标偏移

    /// 检查剩余可读字节是否至少为 @p need 字节。
    /// @throws std::runtime_error 缓冲区溢出时抛出。
    void ensureReadable(int need) const;
};

#endif // BINARYSTREAM_H
