#ifndef CRYPTOUTIL_H
#define CRYPTOUTIL_H

#include <string>
#include <cstdint>

class CryptoUtil {
public:
    // SHA-256 hash → 64-char hex string
    static std::string sha256(const std::string& data);
    static std::string sha256(const char* data, size_t len);

    // File fingerprint: SHA-256 of entire file content
    static std::string fileFingerprint(const std::string& filePath);

    // Sparse fingerprint: SHA-256(head 4KB + tail 4KB + size as big-endian)
    static std::string sparseFingerprint(const std::string& filePath);

    // Password hashing: SHA-256(password + salt)
    static std::string hashPassword(const std::string& password);

private:
    static const std::string PASSWORD_SALT;  // "0323CloudDisk_SALT_2026"

    struct Sha256Ctx {
        uint8_t  data[64];
        uint32_t datalen;
        uint64_t bitlen;
        uint32_t state[8];
    };
    static void sha256Init(Sha256Ctx* ctx);
    static void sha256Update(Sha256Ctx* ctx, const uint8_t* data, size_t len);
    static void sha256Final(Sha256Ctx* ctx, uint8_t hash[32]);
    static void sha256Transform(Sha256Ctx* ctx, const uint8_t data[]);
    static const uint32_t K[64];
};

#endif
