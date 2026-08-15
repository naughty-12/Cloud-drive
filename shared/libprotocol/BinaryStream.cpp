/**
 * @file BinaryStream.cpp
 * @brief Implementation of the BinaryStream serialization engine.
 *
 * All multi-byte integers are converted to/from network byte order (big-endian)
 * using hand-rolled bit operations (anonymous namespace below).
 * No Winsock2 htonl/ntohl dependency — the implementation does NOT call
 * the system byte-swap functions.
 */

#include "BinaryStream.h"
#include <cstring>

// Inline byte-order conversion (no platform dependencies)
// All multi-byte integers are network byte order (big-endian)
namespace {
    inline bool isLittleEndian() {
        const uint16_t v = 1;
        return (*reinterpret_cast<const uint8_t*>(&v) == 1);
    }
    inline uint32_t hostToNet32(uint32_t v) { return isLittleEndian() ? ((v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24)) : v; }
    inline uint32_t netToHost32(uint32_t v) { return hostToNet32(v); }  // symmetric
    inline uint16_t hostToNet16(uint16_t v) { return isLittleEndian() ? (uint16_t)((v >> 8) | (v << 8)) : v; }
    inline uint16_t netToHost16(uint16_t v) { return hostToNet16(v); }
}

// ============================================================================
// Construction
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
// Write Operators
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
    // Manual big-endian: split 64-bit into hi/lo 32-bit halves,
    // convert each to network byte order, append hi then lo.
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
    // 2-byte length prefix in network byte order
    uint16_t len = static_cast<uint16_t>(str.size());
    len = hostToNet16(len);
    const uint8_t* lenBytes = reinterpret_cast<const uint8_t*>(&len);
    m_buffer.insert(m_buffer.end(), lenBytes, lenBytes + sizeof(len));

    // UTF-8 content
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

    // Write exactly fixedSize bytes, zero-padded if str is shorter
    size_t strLen = (str != nullptr) ? std::strlen(str) : 0;
    size_t copyLen = (strLen < static_cast<size_t>(fixedSize)) ? strLen : static_cast<size_t>(fixedSize);

    // Copy string content
    if (copyLen > 0) {
        m_buffer.insert(m_buffer.end(),
                        reinterpret_cast<const uint8_t*>(str),
                        reinterpret_cast<const uint8_t*>(str) + copyLen);
    }

    // Zero-pad remaining bytes
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
// Read Operators
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

    // Read hi and lo 32-bit halves in network byte order
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

    // Read 2-byte length prefix
    uint16_t netLen;
    std::memcpy(&netLen, m_buffer.data() + m_readPos, sizeof(netLen));
    m_readPos += 2;

    uint16_t strLen = netToHost16(netLen);
    ensureReadable(static_cast<int>(strLen));

    // Read string content
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

    // Ensure null termination (in case source data fills the entire buffer)
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
// State Management
// ============================================================================

void BinaryStream::reset()
{
    m_buffer.clear();
    m_readPos = 0;
}
