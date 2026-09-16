#pragma once

#include <cstddef>

namespace lsm {

struct Options {
    // MemTable size threshold in bytes before flushing to SSTable.
    // Default: 4MB
    size_t memtable_size_threshold = 4 * 1024 * 1024;

    // Sparse index interval: index every N-th record in SSTable.
    // Default: 16
    size_t sparse_index_interval = 16;

    // Bloom filter bits per key.
    // Default: 10 (~1% false positive rate)
    size_t bloom_bits_per_key = 10;

    // Number of hash functions for Bloom filter.
    // Default: 7
    size_t bloom_num_hashes = 7;

    // Compaction trigger: compact when number of SSTables reaches this count.
    // Default: 4
    size_t compaction_threshold = 4;

    // Flag to enable or disable bloom filter usage (useful for benchmark comparisons).
    // Default: true
    bool enable_bloom_filter = true;
};

} // namespace lsm
