#include "CryptoUtil.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>

// ─── SHA-256 轮常量 ────────────────────────────────────────────
const uint32_t CryptoUtil::K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

// ─── SHA-256 宏 ─────────────────────────────────────────────────────
#define ROTRIGHT(a,b) (((a)>>(b))|((a)<<(32-(b))))
#define CH(x,y,z)     (((x)&(y))^(~(x)&(z)))
#define MAJ(x,y,z)    (((x)&(y))^((x)&(z))^((y)&(z)))
#define EP0(x)        (ROTRIGHT(x,2)^ROTRIGHT(x,13)^ROTRIGHT(x,22))
#define EP1(x)        (ROTRIGHT(x,6)^ROTRIGHT(x,11)^ROTRIGHT(x,25))
#define SIG0(x)       (ROTRIGHT(x,7)^ROTRIGHT(x,18)^((x)>>3))
#define SIG1(x)       (ROTRIGHT(x,17)^ROTRIGHT(x,19)^((x)>>10))

// ─── 密码盐值 ──────────────────────────────────────────────────────
const std::string CryptoUtil::PASSWORD_SALT = "0323CloudDisk_SALT_2026";

// ─── sha256Init ─────────────────────────────────────────────────────────
void CryptoUtil::sha256Init(Sha256Ctx* ctx) {
    ctx->datalen = 0;
    ctx->bitlen  = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

// ─── sha256Transform ────────────────────────────────────────────────────
void CryptoUtil::sha256Transform(Sha256Ctx* ctx, const uint8_t data[]) {
    uint32_t a, b, c, d, e, f, g, h, t1, t2;
    uint32_t m[64];

    // 从输入块（大端序）准备消息调度表 W[0..15]
    for (int i = 0, j = 0; i < 16; i++, j += 4) {
        m[i] = ((uint32_t)data[j] << 24)
             | ((uint32_t)data[j+1] << 16)
             | ((uint32_t)data[j+2] << 8)
             | ((uint32_t)data[j+3]);
    }

    // 扩展至 W[16..63]
    for (int i = 16; i < 64; i++) {
        m[i] = SIG1(m[i-2]) + m[i-7] + SIG0(m[i-15]) + m[i-16];
    }

    // 初始化工作变量
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    // 64 轮迭代
    for (int i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e,f,g) + K[i] + m[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    // 更新状态
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

// ─── sha256Update ───────────────────────────────────────────────────────
void CryptoUtil::sha256Update(Sha256Ctx* ctx, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            sha256Transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

// ─── sha256Final ────────────────────────────────────────────────────────
void CryptoUtil::sha256Final(Sha256Ctx* ctx, uint8_t hash[32]) {
    uint32_t i = ctx->datalen;

    // 填充：追加 0x80
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56)
            ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64)
            ctx->data[i++] = 0x00;
        sha256Transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    // 以 64 位大端序追加总比特长度
    // （datalen 为 uint32_t：相乘前先转换，避免 32 位溢出
    //  - 由 clang-tidy bugprone-implicit-widening-of-multiplication-result 检查标记）
    ctx->bitlen += static_cast<uint64_t>(ctx->datalen) * 8;
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[63] = (uint8_t)(ctx->bitlen);

    sha256Transform(ctx, ctx->data);

    // 以大端序输出 32 字节
    for (i = 0; i < 4; i++) {
        hash[i]      = (uint8_t)((ctx->state[0] >> (24 - i*8)) & 0xff);
        hash[i+4]    = (uint8_t)((ctx->state[1] >> (24 - i*8)) & 0xff);
        hash[i+8]    = (uint8_t)((ctx->state[2] >> (24 - i*8)) & 0xff);
        hash[i+12]   = (uint8_t)((ctx->state[3] >> (24 - i*8)) & 0xff);
        hash[i+16]   = (uint8_t)((ctx->state[4] >> (24 - i*8)) & 0xff);
        hash[i+20]   = (uint8_t)((ctx->state[5] >> (24 - i*8)) & 0xff);
        hash[i+24]   = (uint8_t)((ctx->state[6] >> (24 - i*8)) & 0xff);
        hash[i+28]   = (uint8_t)((ctx->state[7] >> (24 - i*8)) & 0xff);
    }
}

// ─── sha256FinalHex ─────────────────────────────────────────────────────
std::string CryptoUtil::sha256FinalHex(Sha256Ctx* ctx) {
    uint8_t hash[32];
    sha256Final(ctx, hash);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 32; i++) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }
    return oss.str();
}

// ─── sha256 (string) ────────────────────────────────────────────────────
std::string CryptoUtil::sha256(const std::string& data) {
    return sha256(data.c_str(), data.size());
}

// ─── sha256 (data, len) ─────────────────────────────────────────────────
std::string CryptoUtil::sha256(const char* data, size_t len) {
    Sha256Ctx ctx;
    uint8_t hash[32];

    sha256Init(&ctx);
    sha256Update(&ctx, reinterpret_cast<const uint8_t*>(data), len);
    sha256Final(&ctx, hash);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 32; i++) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }
    return oss.str();
}

// ─── fileFingerprint ────────────────────────────────────────────────────
std::string CryptoUtil::fileFingerprint(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    Sha256Ctx ctx;
    uint8_t hash[32];
    char buffer[8192];

    sha256Init(&ctx);
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        sha256Update(&ctx, reinterpret_cast<const uint8_t*>(buffer),
                     static_cast<size_t>(file.gcount()));
    }
    sha256Final(&ctx, hash);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 32; i++) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }
    return oss.str();
}

// ─── sparseFingerprint ──────────────────────────────────────────────────
std::string CryptoUtil::sparseFingerprint(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return "";
    }

    std::streamsize fileSize = file.tellg();
    if (fileSize <= 0) {
        return sha256("");
    }

    Sha256Ctx ctx;
    sha256Init(&ctx);

    // 读取头部 4KB
    const size_t HEAD_SIZE = 4096;
    char buffer[4096];
    file.seekg(0, std::ios::beg);
    size_t headRead = 0;
    if (fileSize >= static_cast<std::streamsize>(HEAD_SIZE)) {
        file.read(buffer, HEAD_SIZE);
        headRead = static_cast<size_t>(file.gcount());
    } else {
        file.read(buffer, fileSize);
        headRead = static_cast<size_t>(file.gcount());
    }
    sha256Update(&ctx, reinterpret_cast<const uint8_t*>(buffer), headRead);

    // 读取尾部 4KB
    const size_t TAIL_SIZE = 4096;
    if (fileSize > static_cast<std::streamsize>(TAIL_SIZE)) {
        file.seekg(-static_cast<std::streamoff>(TAIL_SIZE), std::ios::end);
    } else {
        file.seekg(0, std::ios::beg);
    }
    file.read(buffer, TAIL_SIZE);
    size_t tailRead = static_cast<size_t>(file.gcount());
    sha256Update(&ctx, reinterpret_cast<const uint8_t*>(buffer), tailRead);

    // 以 8 字节大端序哈希文件大小
    uint64_t size = static_cast<uint64_t>(fileSize);
    uint8_t sizeBytes[8];
    for (int i = 0; i < 8; i++) {
        sizeBytes[i] = static_cast<uint8_t>((size >> (56 - i*8)) & 0xff);
    }
    sha256Update(&ctx, sizeBytes, 8);

    uint8_t hash[32];
    sha256Final(&ctx, hash);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 32; i++) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }
    return oss.str();
}

// ─── sparseFingerprintFromBlocks ──────────────────────────────────────────
// 服务端：从已上传的数据块计算稀疏指纹。
// head 与 tail 是从 FileStorage 读取的原始首/尾块。
std::string CryptoUtil::sparseFingerprintFromBlocks(const std::string& head,
                                                     const std::string& tail,
                                                     uint64_t fileSize) {
    const size_t HEAD_SIZE = 4096;
    const size_t TAIL_SIZE = 4096;

    Sha256Ctx ctx;
    sha256Init(&ctx);

    // 哈希头部（最多 4KB）
    size_t headLen = std::min(head.size(), HEAD_SIZE);
    sha256Update(&ctx, reinterpret_cast<const uint8_t*>(head.data()), headLen);

    // 哈希尾部（最多 4KB）—— 若文件小于 4KB，头尾重叠也无妨
    if (!tail.empty()) {
        size_t tailLen = std::min(tail.size(), TAIL_SIZE);
        sha256Update(&ctx, reinterpret_cast<const uint8_t*>(tail.data()), tailLen);
    }

    // 以 8 字节大端序哈希文件大小
    uint8_t sizeBytes[8];
    for (int i = 0; i < 8; i++) {
        sizeBytes[i] = static_cast<uint8_t>((fileSize >> (56 - i*8)) & 0xff);
    }
    sha256Update(&ctx, sizeBytes, 8);

    uint8_t hash[32];
    sha256Final(&ctx, hash);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 32; i++) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }
    return oss.str();
}

// ─── hashPassword ───────────────────────────────────────────────────────
std::string CryptoUtil::hashPassword(const std::string& password) {
    return sha256(password + PASSWORD_SALT);
}

#undef ROTRIGHT
#undef CH
#undef MAJ
#undef EP0
#undef EP1
#undef SIG0
#undef SIG1
