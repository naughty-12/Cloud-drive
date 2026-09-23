/**
 * @file BinaryStream.cpp
 * @brief BinaryStream 二进制读写器的实现。
 *
 * 所有多字节整数均通过手写位运算（见下方匿名命名空间）在网络字节序（大端）
 * 与主机字节序之间转换。
 * 不依赖 Winsock2 的 htonl/ntohl —— 本实现不调用任何系统的字节交换函数。
 */

#include "BinaryStream.h"
#include <cstring>

// 内联字节序转换（无平台依赖）
// 所有多字节整数均为网络字节序（大端）
namespace {
    inline bool isLittleEndian() {
        const uint16_t v = 1;
        return (*reinterpret_cast<const uint8_t*>(&v) == 1);
    }
    inline uint32_t hostToNet32(uint32_t v) { return isLittleEndian() ? ((v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24)) : v; }
    inline uint32_t netToHost32(uint32_t v) { return hostToNet32(v); }  // 对称转换
    inline uint16_t hostToNet16(uint16_t v) { return isLittleEndian() ? (uint16_t)((v >> 8) | (v << 8)) : v; }
    inline uint16_t netToHost16(uint16_t v) { return hostToNet16(v); }
}

// ============================================================================
// 构造
// ============================================================================

BinaryStream::BinaryStream()
    : m_readPos(0)
{
}

BinaryStream BinaryStream::fromData(const char* data, int len)
{
    BinaryStream bs;
    if (data && len > 0) {
        bs.m_buffer.assign(reinterpret_cast<const uint8_t*>(data),
                           reinterpret_cast<const uint8_t*>(data) + len);
    }
    bs.m_readPos = 0;
    return bs;
}

// ============================================================================
// 写操作
// ============================================================================

BinaryStream& BinaryStream::operator<<(int32_t val)
{
    uint32_t netVal = hostToNet32(static_cast<uint32_t>(val));
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&netVal);
    m_buffer.insert(m_buffer.end(), bytes, bytes + sizeof(netVal));
    return *this;
}

BinaryStream& BinaryStream::operator<<(int64_t val)
{
    // 手写大端：将 64 位拆分为高、低两个 32 位半段，
    // 各自转换为网络字节序后，先追加高 32 位，再追加低 32 位。
    uint64_t uval = static_cast<uint64_t>(val);
    uint32_t hi = static_cast<uint32_t>(uval >> 32);
    uint32_t lo = static_cast<uint32_t>(uval & 0xFFFFFFFF);
    hi = hostToNet32(hi);
    lo = hostToNet32(lo);

    const uint8_t* hiBytes = reinterpret_cast<const uint8_t*>(&hi);
    const uint8_t* loBytes = reinterpret_cast<const uint8_t*>(&lo);
    m_buffer.insert(m_buffer.end(), hiBytes, hiBytes + sizeof(hi));
    m_buffer.insert(m_buffer.end(), loBytes, loBytes + sizeof(lo));
    return *this;
}

BinaryStream& BinaryStream::operator<<(const std::string& str)
{
    // 2 字节长度前缀，网络字节序
    uint16_t len = static_cast<uint16_t>(str.size());
    len = hostToNet16(len);
    const uint8_t* lenBytes = reinterpret_cast<const uint8_t*>(&len);
    m_buffer.insert(m_buffer.end(), lenBytes, lenBytes + sizeof(len));

    // UTF-8 内容
    if (!str.empty()) {
        m_buffer.insert(m_buffer.end(),
                        reinterpret_cast<const uint8_t*>(str.data()),
                        reinterpret_cast<const uint8_t*>(str.data()) + str.size());
    }
    return *this;
}

BinaryStream& BinaryStream::writeFixedString(const char* str, int fixedSize)
{
    if (fixedSize <= 0) return *this;

    // 恰好写入 fixedSize 字节；若 str 较短，剩余部分补 0
    size_t strLen = (str != nullptr) ? std::strlen(str) : 0;
    size_t copyLen = (strLen < static_cast<size_t>(fixedSize)) ? strLen : static_cast<size_t>(fixedSize);

    // 拷贝字符串内容
    if (copyLen > 0) {
        m_buffer.insert(m_buffer.end(),
                        reinterpret_cast<const uint8_t*>(str),
                        reinterpret_cast<const uint8_t*>(str) + copyLen);
    }

    // 剩余字节补 0
    size_t padLen = static_cast<size_t>(fixedSize) - copyLen;
    if (padLen > 0) {
        m_buffer.insert(m_buffer.end(), padLen, 0);
    }

    return *this;
}

void BinaryStream::writeRaw(const char* data, int len)
{
    if (data && len > 0) {
        m_buffer.insert(m_buffer.end(),
                        reinterpret_cast<const uint8_t*>(data),
                        reinterpret_cast<const uint8_t*>(data) + len);
    }
}

// ============================================================================
// 读操作
// ============================================================================

void BinaryStream::ensureReadable(int need) const
{
    if (m_readPos + need > static_cast<int>(m_buffer.size())) {
        throw std::runtime_error(
            "BinaryStream: buffer overflow — tried to read " +
            std::to_string(need) + " bytes at position " +
            std::to_string(m_readPos) + " but only " +
            std::to_string(static_cast<int>(m_buffer.size()) - m_readPos) +
            " bytes remain");
    }
}

BinaryStream& BinaryStream::operator>>(int32_t& val)
{
    ensureReadable(4);

    uint32_t netVal;
    std::memcpy(&netVal, m_buffer.data() + m_readPos, sizeof(netVal));
    m_readPos += 4;

    val = static_cast<int32_t>(netToHost32(netVal));
    return *this;
}

BinaryStream& BinaryStream::operator>>(int64_t& val)
{
    ensureReadable(8);

    // 按网络字节序读取高、低两个 32 位半段
    uint32_t hi, lo;
    std::memcpy(&hi, m_buffer.data() + m_readPos, sizeof(hi));
    std::memcpy(&lo, m_buffer.data() + m_readPos + 4, sizeof(lo));
    m_readPos += 8;

    hi = netToHost32(hi);
    lo = netToHost32(lo);

    uint64_t uval = (static_cast<uint64_t>(hi) << 32) | lo;
    val = static_cast<int64_t>(uval);
    return *this;
}

BinaryStream& BinaryStream::operator>>(std::string& str)
{
    ensureReadable(2);

    // 读取 2 字节长度前缀
    uint16_t netLen;
    std::memcpy(&netLen, m_buffer.data() + m_readPos, sizeof(netLen));
    m_readPos += 2;

    uint16_t strLen = netToHost16(netLen);
    ensureReadable(static_cast<int>(strLen));

    // 读取字符串内容
    if (strLen > 0) {
        str.assign(reinterpret_cast<const char*>(m_buffer.data() + m_readPos), strLen);
        m_readPos += strLen;
    } else {
        str.clear();
    }

    return *this;
}

BinaryStream& BinaryStream::readFixedString(char* str, int fixedSize)
{
    if (fixedSize <= 0) return *this;

    ensureReadable(fixedSize);

    std::memcpy(str, m_buffer.data() + m_readPos, fixedSize);
    m_readPos += fixedSize;

    // 确保以 '\0' 结尾（防止源数据占满整个缓冲区时缺少终止符）
    str[fixedSize - 1] = '\0';

    return *this;
}

void BinaryStream::readRaw(char* data, int len)
{
    if (len <= 0) return;

    ensureReadable(len);
    std::memcpy(data, m_buffer.data() + m_readPos, len);
    m_readPos += len;
}

// ============================================================================
// 状态管理
// ============================================================================

void BinaryStream::reset()
{
    m_buffer.clear();
    m_readPos = 0;
}
