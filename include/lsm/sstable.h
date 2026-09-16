#pragma once

#include "lsm/types.h"
#include "lsm/status.h"
#include "lsm/options.h"
#include "lsm/memtable.h"
#include "lsm/bloom_filter.h"
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <cstdint>

namespace lsm {

// 8-byte magic numbers
constexpr uint64_t kSSTableHeaderMagic = 0x4C534D5452454531ULL; // "LSMTREE1"
constexpr uint64_t kSSTableFooterMagic = 0x4C534D5353544654ULL; // "LSMSSTFT"
constexpr uint32_t kSSTableVersion = 1;
constexpr size_t kSSTableHeaderSize = 16;
constexpr size_t kSSTableFooterSize = 64;

struct IndexEntry {
    std::string key;
    uint64_t offset;
};

class SSTableIndex {
public:
    SSTableIndex() = default;

    void add(std::string key, uint64_t offset);
    std::vector<uint8_t> serialize() const;
    static SSTableIndex deserialize(const uint8_t* data, size_t size);

    // Given a key, find [start_offset, end_offset) to search in the SSTable file
    void findOffsetRange(
        const std::string& key,
        uint64_t& start_offset,
        uint64_t& end_offset,
        uint64_t data_end_offset
    ) const;

    size_t size() const { return entries_.size(); }
    const std::vector<IndexEntry>& entries() const { return entries_; }

private:
    std::vector<IndexEntry> entries_;
};

struct SSTableFooter {
    uint64_t data_offset = 0;
    uint64_t data_size = 0;
    uint64_t index_offset = 0;
    uint64_t index_size = 0;
    uint64_t bloom_offset = 0;
    uint64_t bloom_size = 0;
    uint32_t record_count = 0;
    uint32_t flags = 0;

    std::vector<uint8_t> serialize() const;
    static Status deserialize(const uint8_t* data, size_t size, SSTableFooter& out_footer);
};

class SSTableWriter {
public:
    explicit SSTableWriter(const Options& options = Options{});
    ~SSTableWriter();

    Status open(const std::string& filepath, size_t estimated_keys = 0);
    Status append(const std::string& key, const std::string& value, RecordType type = RecordType::PUT);
    Status finish();

    // Helper: directly write a MemTable to SSTable file
    static Status writeFromMemTable(
        const std::string& filepath,
        const MemTable& memtable,
        const Options& options = Options{}
    );

    // Helper: directly write sorted entries to SSTable file (used by compaction)
    static Status writeFromEntries(
        const std::string& filepath,
        const std::vector<KeyValueEntry>& entries,
        const Options& options = Options{}
    );

    size_t recordCount() const { return record_count_; }

private:
    Options options_;
    std::string filepath_;
    std::ofstream file_;
    bool is_open_;
    uint64_t current_offset_;
    uint32_t record_count_;
    std::string last_key_;

    SSTableIndex index_;
    BloomFilter bloom_filter_;
};

class SSTableReader {
public:
    SSTableReader();
    ~SSTableReader();

    // Prevent copying
    SSTableReader(const SSTableReader&) = delete;
    SSTableReader& operator=(const SSTableReader&) = delete;

    // Allow moving
    SSTableReader(SSTableReader&&) noexcept;
    SSTableReader& operator=(SSTableReader&&) noexcept;

    // Open and load metadata, index, and bloom filter into memory
    Status open(const std::string& filepath, bool enable_bloom_filter = true);
    Status close();

    // Look up a key
    Result get(const std::string& key, bool* is_tombstone = nullptr) const;

    // Read all records in sorted order (used for compaction and full scans)
    Status readAllRecords(std::vector<KeyValueEntry>& out_records) const;

    const std::string& filepath() const { return filepath_; }
    uint32_t recordCount() const { return footer_.record_count; }
    const SSTableIndex& index() const { return index_; }
    const BloomFilter& bloomFilter() const { return bloom_filter_; }
    bool isOpen() const { return is_open_; }

private:
    std::string filepath_;
    bool is_open_;
    bool enable_bloom_filter_;
    SSTableFooter footer_;
    SSTableIndex index_;
    BloomFilter bloom_filter_;

    Result scanRange(uint64_t start_offset, uint64_t end_offset, const std::string& key, bool* is_tombstone) const;
};

} // namespace lsm
