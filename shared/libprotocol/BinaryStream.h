#ifndef BINARYSTREAM_H
#define BINARYSTREAM_H

/**
 * @file BinaryStream.h
 * @brief Lightweight binary serialization engine for network protocols.
 *
 * BinaryStream provides a simple, type-safe way to serialize and deserialize
 * binary protocol packets. All multi-byte integers are stored in network byte
 * order (big-endian) using hand-rolled bit operations (zero system-call dependency;
 * see anonymous namespace in BinaryStream.cpp).
 *
 * Design goals:
 *   - Zero external dependencies (only <cstdint>, <string>, <vector>, <cstring>)
 *   - RAII-safe buffer management (std::vector<uint8_t>)
 *   - Bounds-checked reads (throws std::runtime_error on overflow)
 *   - Compatible with the existing [4-byte length][payload] wire format
 *
 * Usage example:
 * @code
 *   // Writing
 *   BinaryStream bs;
 *   bs << (int32_t)42 << std::string("hello");
 *   auto packet = ProtocolFactory::wrapPacket(bs.data());
 *   send(sock, packet.data(), packet.size());
 *
 *   // Reading
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

// Forward declaration for Packdef.h MAXSIZE constant
// (BinaryStream does not depend on Packdef.h; the fixed-size
// string read/write methods accept size as a parameter.)
#ifndef MAXSIZE
#define BINSTREAM_DEFAULT_FIXED_STR 45
#endif

class BinaryStream {
public:
    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    /// Construct an empty stream ready for writing.
    BinaryStream();

    /// Construct a stream from raw buffer data for reading.
    /// @param data Pointer to the raw byte buffer (not owned).
    /// @param len  Number of bytes to copy into the internal buffer.
    static BinaryStream fromData(const char* data, int len);

    // -----------------------------------------------------------------------
    // Write Operators (Serialization → network byte order)
    // -----------------------------------------------------------------------

    /// Write a 32-bit signed integer (hand-rolled byte swap → 4 bytes big-endian).
    /// @param val The value to write.
    /// @return Reference to this stream for chaining.
    BinaryStream& operator<<(int32_t val);

    /// Write a 64-bit signed integer (manual big-endian: hi/lo 32-bit swap, no htonll needed).
    /// @param val The value to write.
    /// @return Reference to this stream for chaining.
    BinaryStream& operator<<(int64_t val);

    /// Write a variable-length string (2-byte length prefix + UTF-8 content).
    /// The length prefix uses hand-rolled 16-bit byte swap (network byte order).
    /// @param str The string to write. Maximum length is 65535 bytes.
    /// @return Reference to this stream for chaining.
    BinaryStream& operator<<(const std::string& str);

    /// Write a fixed-size char array (exactly @p fixedSize bytes).
    /// If the source string is shorter, the remainder is zero-padded.
    /// This is used for legacy MAXSIZE (45-byte) string fields.
    /// @param str Null-terminated C string to write.
    /// @param fixedSize Number of bytes to write (default: MAXSIZE from Packdef.h, or 45).
    /// @return Reference to this stream for chaining.
    /// @note This is a named method, not operator<<, because the fixed size
    ///       must be explicitly specified for correctness.
    BinaryStream& writeFixedString(const char* str, int fixedSize);

    /// Write raw bytes directly into the buffer (no length prefix, no endianness conversion).
    /// @param data Pointer to the raw bytes.
    /// @param len  Number of bytes to write.
    void writeRaw(const char* data, int len);

    // -----------------------------------------------------------------------
    // Read Operators (Deserialization → host byte order)
    // -----------------------------------------------------------------------

    /// Read a 32-bit signed integer (4 bytes → ntohl → host order).
    /// @param val [out] The value read.
    /// @return Reference to this stream for chaining.
    /// @throws std::runtime_error if fewer than 4 bytes remain.
    BinaryStream& operator>>(int32_t& val);

    /// Read a 64-bit signed integer (8 bytes → manual ntohll → host order).
    /// @param val [out] The value read.
    /// @return Reference to this stream for chaining.
    /// @throws std::runtime_error if fewer than 8 bytes remain.
    BinaryStream& operator>>(int64_t& val);

    /// Read a variable-length string (2-byte length prefix → ntohs → read content).
    /// @param str [out] The string read.
    /// @return Reference to this stream for chaining.
    /// @throws std::runtime_error if the declared length exceeds remaining bytes.
    BinaryStream& operator>>(std::string& str);

    /// Read exactly @p fixedSize bytes into a char buffer.
    /// @param str [out] Destination buffer (must be at least @p fixedSize bytes).
    /// @param fixedSize Number of bytes to read.
    /// @return Reference to this stream for chaining.
    /// @throws std::runtime_error if fewer than @p fixedSize bytes remain.
    /// @note This is a named method for clarity — the fixed size is explicit.
    BinaryStream& readFixedString(char* str, int fixedSize);

    /// Read raw bytes from the buffer.
    /// @param data [out] Destination buffer.
    /// @param len  Number of bytes to read.
    /// @throws std::runtime_error if fewer than @p len bytes remain.
    void readRaw(char* data, int len);

    // -----------------------------------------------------------------------
    // Buffer Access
    // -----------------------------------------------------------------------

    /// Get the internal buffer (const reference for efficiency).
    /// @return The complete serialized byte vector.
    const std::vector<uint8_t>& buffer() const { return m_buffer; }

    /// Get a copy of the internal buffer.
    /// @return A copy of the serialized byte vector.
    std::vector<uint8_t> data() const { return m_buffer; }

    /// Get a const pointer to the raw buffer data.
    /// @return Pointer to the first byte, or nullptr if empty.
    const uint8_t* dataPtr() const { return m_buffer.data(); }

    /// Get the total buffer size in bytes.
    /// @return Number of bytes in the buffer.
    int size() const { return static_cast<int>(m_buffer.size()); }

    /// Get the current read position.
    /// @return Byte offset of the next read.
    int readPos() const { return m_readPos; }

    /// Get the number of unread bytes remaining.
    /// @return size() - readPos().
    int remaining() const { return static_cast<int>(m_buffer.size()) - m_readPos; }

    // -----------------------------------------------------------------------
    // State Management
    // -----------------------------------------------------------------------

    /// Reset the stream to empty (clears buffer and read position).
    void reset();

private:
    std::vector<uint8_t> m_buffer;   ///< Internal byte storage
    int                  m_readPos;  ///< Current read cursor offset

    /// Check that at least @p need bytes remain for reading.
    /// @throws std::runtime_error on buffer overflow.
    void ensureReadable(int need) const;
};

#endif // BINARYSTREAM_H
