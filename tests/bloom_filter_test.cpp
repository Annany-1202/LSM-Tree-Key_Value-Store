#include "test_framework.h"
#include "lsm/bloom_filter.h"
#include <string>
#include <vector>

TEST(BloomFilterTest, NoFalseNegatives) {
    const size_t kNumKeys = 1000;
    lsm::BloomFilter filter(kNumKeys, 10, 7);

    std::vector<std::string> keys;
    for (size_t i = 0; i < kNumKeys; ++i) {
        std::string key = "key_" + std::to_string(i);
        keys.push_back(key);
        filter.add(key);
    }

    // Every inserted key MUST be reported as possibly present (no false negatives!)
    for (const auto& key : keys) {
        ASSERT_TRUE(filter.possiblyContains(key));
    }
}

TEST(BloomFilterTest, FalsePositiveRateIsLow) {
    const size_t kNumKeys = 1000;
    lsm::BloomFilter filter(kNumKeys, 10, 7);

    for (size_t i = 0; i < kNumKeys; ++i) {
        filter.add("present_key_" + std::to_string(i));
    }

    size_t false_positives = 0;
    const size_t kNumQueries = 1000;
    for (size_t i = 0; i < kNumQueries; ++i) {
        std::string absent_key = "absent_key_" + std::to_string(i);
        if (filter.possiblyContains(absent_key)) {
            false_positives++;
        }
    }

    // For 10 bits per key and 7 hash functions, theoretical FP rate is ~1%
    // In 1000 queries, FP count should be comfortably below 5% (50)
    ASSERT_TRUE(false_positives < 50);
}

TEST(BloomFilterTest, SerializationAndDeserialization) {
    lsm::BloomFilter original(100, 10, 7);
    original.add("apple");
    original.add("banana");
    original.add("orange");

    std::vector<uint8_t> serialized = original.serialize();
    ASSERT_TRUE(!serialized.empty());

    lsm::BloomFilter loaded = lsm::BloomFilter::deserialize(serialized.data(), serialized.size());
    ASSERT_EQ(loaded.numHashes(), original.numHashes());
    ASSERT_EQ(loaded.bitCount(), original.bitCount());

    ASSERT_TRUE(loaded.possiblyContains("apple"));
    ASSERT_TRUE(loaded.possiblyContains("banana"));
    ASSERT_TRUE(loaded.possiblyContains("orange"));
    ASSERT_FALSE(loaded.possiblyContains("completely_non_existent_key_xyz"));
}
