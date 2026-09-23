#ifndef BLOOMFILTER_H
#define BLOOMFILTER_H

/**
 * @file BloomFilter.h
 * @brief 空间高效的集合成员概率判定。
 *
 * 用作秒传漏斗的 L3 层。服务器启动时将全部已知的 SHA-256 文件指纹
 * 加载进内存（warmupBloomFilter）。查询复杂度为 O(k)，
 * k 为哈希函数个数（误判率 0.1% 时约 7 个）。
 *
 * 100 万文件、0.1% 误判率下约占用 1.7 MB 内存。使用 FNV-1a 64 位哈希
 * 配合双重哈希生成 k 个哈希位置。
 */

#include <string>
#include <vector>
#include <cstdint>

class BloomFilter {
public:
    // n：预期元素数量，p：误判率（默认 0.001 = 0.1%）
    BloomFilter(size_t expectedElements = 1000000, double falsePositiveRate = 0.001);

    void insert(const std::string& key);
    bool mightContain(const std::string& key) const;

    size_t size() const { return m_bits.size(); }
    size_t hashCount() const { return m_hashCount; }

private:
    std::vector<bool> m_bits;
    size_t m_hashCount;

    // 双重哈希：h(i, key) = (hash1(key) + i * hash2(key)) % m
    uint64_t hash1(const std::string& key) const;
    uint64_t hash2(const std::string& key) const;
    uint64_t fnv1a64(const std::string& key) const;
};

#endif
