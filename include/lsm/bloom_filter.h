#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>

namespace lsm {

class BloomFilter {
public:
    // Construct with estimated number of keys, bits per key, and number of hash functions
    BloomFilter(size_t num_keys = 0, size_t bits_per_key = 10, size_t num_hashes = 7);
    ~BloomFilter() = default;

    // Add key to bloom filter
    void add(const std::string& key);

    // Test if key might be in the set
    // Returns false -> definitely NOT in set (no false negatives)
    // Returns true -> possibly in set (may have false positives)
    bool possiblyContains(const std::string& key) const;

    // Serialize filter to byte buffer
    std::vector<uint8_t> serialize() const;

    // Deserialize from byte buffer
    static BloomFilter deserialize(const uint8_t* data, size_t length);

    size_t bitCount() const { return bits_.size() * 8; }
    size_t byteCount() const { return bits_.size(); }
    uint32_t numHashes() const { return num_hashes_; }

private:
    std::vector<uint8_t> bits_;
    uint32_t num_hashes_;

    // Hash helpers
    static uint32_t hash1(const std::string& key);
    static uint32_t hash2(const std::string& key);
};

} // namespace lsm
