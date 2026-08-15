#ifndef BLOOMFILTER_H
#define BLOOMFILTER_H

/**
 * @file BloomFilter.h
 * @brief Space-efficient probabilistic set membership test.
 *
 * Used as L3 in the instant-upload (秒传) funnel. Server loads all known
 * SHA-256 file fingerprints on startup (warmupBloomFilter).  Query is O(k)
 * where k = number of hash functions (~7 for 0.1% false positive rate).
 *
 * For 1M files at 0.1% FP rate: ~1.7 MB memory.  Uses FNV-1a 64-bit hash
 * with double hashing to generate k hash positions.
 */

#include <string>
#include <vector>
#include <cstdint>

class BloomFilter {
public:
    // n: expected elements, p: false positive rate (default 0.001 = 0.1%)
    BloomFilter(size_t expectedElements = 1000000, double falsePositiveRate = 0.001);

    void insert(const std::string& key);
    bool mightContain(const std::string& key) const;

    size_t size() const { return m_bits.size(); }
    size_t hashCount() const { return m_hashCount; }

private:
    std::vector<bool> m_bits;
    size_t m_hashCount;

    // Double hashing: h(i, key) = (hash1(key) + i * hash2(key)) % m
    uint64_t hash1(const std::string& key) const;
    uint64_t hash2(const std::string& key) const;
    uint64_t fnv1a64(const std::string& key) const;
};

#endif
