#include "BloomFilter.h"
#include <cmath>
#include <cstdio>

BloomFilter::BloomFilter(size_t expectedElements, double falsePositiveRate)
{
    // Calculate optimal bit array size m and hash count k:
    //   m = -(n * ln(p)) / (ln(2)^2)
    //   k = (m / n) * ln(2)

    double n = static_cast<double>(expectedElements);
    double p = falsePositiveRate;

    double m = -(n * std::log(p)) / (std::log(2.0) * std::log(2.0));
    double k = (m / n) * std::log(2.0);

    size_t numBits = static_cast<size_t>(std::ceil(m));
    size_t numHashes = static_cast<size_t>(std::ceil(k));

    // Minimum 1 hash function, minimum 1 bit
    if (numBits < 1) numBits = 1;
    if (numHashes < 1) numHashes = 1;

    m_bits.resize(numBits, false);
    m_hashCount = numHashes;

    printf("BloomFilter: init n=%zu p=%.4f -> m=%zu bits (~%.1f MB) k=%zu hashes\n",
           expectedElements,
           falsePositiveRate,
           m_bits.size(),
           static_cast<double>(m_bits.size()) / 8.0 / 1024.0 / 1024.0,
           m_hashCount);
}

uint64_t BloomFilter::fnv1a64(const std::string& key) const
{
    // FNV-1a 64-bit hash
    const uint64_t FNV_OFFSET = 14695981039346656037ULL;
    const uint64_t FNV_PRIME  = 1099511628211ULL;

    uint64_t hash = FNV_OFFSET;
    for (size_t i = 0; i < key.size(); i++) {
        hash ^= static_cast<uint64_t>(static_cast<uint8_t>(key[i]));
        hash *= FNV_PRIME;
    }
    return hash;
}

uint64_t BloomFilter::hash1(const std::string& key) const
{
    return fnv1a64(key);
}

uint64_t BloomFilter::hash2(const std::string& key) const
{
    // Derive second hash from the first: use the lower and upper 32 bits
    uint64_t h = fnv1a64(key);
    return (h >> 32) ^ (h & 0xFFFFFFFFULL);
}

void BloomFilter::insert(const std::string& key)
{
    uint64_t h1 = hash1(key);
    uint64_t h2 = hash2(key);
    size_t m = m_bits.size();

    // Ensure h2 is odd so we don't get stuck
    if (h2 % 2 == 0) {
        h2 |= 1;
    }

    for (size_t i = 0; i < m_hashCount; i++) {
        // Double hashing: h(i, key) = (hash1(key) + i * hash2(key)) % m
        size_t pos = static_cast<size_t>((h1 + i * h2) % m);
        m_bits[pos] = true;
    }
}

bool BloomFilter::mightContain(const std::string& key) const
{
    uint64_t h1 = hash1(key);
    uint64_t h2 = hash2(key);
    size_t m = m_bits.size();

    // Ensure h2 is odd
    uint64_t h2Copy = h2;
    if (h2Copy % 2 == 0) {
        h2Copy |= 1;
    }

    for (size_t i = 0; i < m_hashCount; i++) {
        size_t pos = static_cast<size_t>((h1 + i * h2Copy) % m);
        if (!m_bits[pos]) {
            return false;  // Definitely not in the set
        }
    }
    return true;  // Might be in the set (possible false positive)
}
