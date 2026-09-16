#include "lsm/bloom_filter.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace lsm {

static void encodeUint32LE(uint32_t value, uint8_t* buffer) {
    buffer[0] = static_cast<uint8_t>(value & 0xFF);
    buffer[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buffer[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buffer[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

static uint32_t decodeUint32LE(const uint8_t* buffer) {
    return static_cast<uint32_t>(buffer[0]) |
           (static_cast<uint32_t>(buffer[1]) << 8) |
           (static_cast<uint32_t>(buffer[2]) << 16) |
           (static_cast<uint32_t>(buffer[3]) << 24);
}

BloomFilter::BloomFilter(size_t num_keys, size_t bits_per_key, size_t num_hashes)
    : num_hashes_(static_cast<uint32_t>(num_hashes)) {
    if (num_keys == 0) {
        num_keys = 64; // default minimum capacity
    }
    size_t total_bits = num_keys * bits_per_key;
    if (total_bits < 64) total_bits = 64;
    size_t num_bytes = (total_bits + 7) / 8;
    bits_.resize(num_bytes, 0);
}

uint32_t BloomFilter::hash1(const std::string& key) {
    // FNV-1a hash
    uint32_t hash = 2166136261u;
    for (char c : key) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

uint32_t BloomFilter::hash2(const std::string& key) {
    // Jenkins one-at-a-time hash
    uint32_t hash = 0;
    for (char c : key) {
        hash += static_cast<uint8_t>(c);
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash == 0 ? 1 : hash;
}

void BloomFilter::add(const std::string& key) {
    if (bits_.empty()) return;
    size_t num_bits = bitCount();
    uint32_t h1 = hash1(key);
    uint32_t h2 = hash2(key);

    for (uint32_t i = 0; i < num_hashes_; ++i) {
        size_t bit_idx = (static_cast<uint64_t>(h1) + static_cast<uint64_t>(i) * h2) % num_bits;
        bits_[bit_idx / 8] |= (1 << (bit_idx % 8));
    }
}

bool BloomFilter::possiblyContains(const std::string& key) const {
    if (bits_.empty()) return true;
    size_t num_bits = bitCount();
    uint32_t h1 = hash1(key);
    uint32_t h2 = hash2(key);

    for (uint32_t i = 0; i < num_hashes_; ++i) {
        size_t bit_idx = (static_cast<uint64_t>(h1) + static_cast<uint64_t>(i) * h2) % num_bits;
        if (!(bits_[bit_idx / 8] & (1 << (bit_idx % 8)))) {
            return false;
        }
    }
    return true;
}

std::vector<uint8_t> BloomFilter::serialize() const {
    std::vector<uint8_t> buffer(8 + bits_.size());
    encodeUint32LE(num_hashes_, &buffer[0]);
    encodeUint32LE(static_cast<uint32_t>(bits_.size()), &buffer[4]);
    if (!bits_.empty()) {
        std::memcpy(&buffer[8], bits_.data(), bits_.size());
    }
    return buffer;
}

BloomFilter BloomFilter::deserialize(const uint8_t* data, size_t length) {
    if (length < 8) {
        throw std::runtime_error("Invalid Bloom filter buffer size");
    }
    uint32_t num_hashes = decodeUint32LE(data);
    uint32_t num_bytes = decodeUint32LE(data + 4);

    if (length < 8 + num_bytes) {
        throw std::runtime_error("Bloom filter data truncated");
    }

    BloomFilter filter;
    filter.num_hashes_ = num_hashes;
    filter.bits_.resize(num_bytes);
    if (num_bytes > 0) {
        std::memcpy(filter.bits_.data(), data + 8, num_bytes);
    }
    return filter;
}

} // namespace lsm
