#pragma once

#include "lsm/types.h"
#include "lsm/status.h"
#include <map>
#include <string>
#include <cstddef>
#include <vector>

namespace lsm {

struct MemTableEntry {
    std::string value;
    RecordType type = RecordType::PUT;

    bool isTombstone() const { return type == RecordType::TOMBSTONE; }
};

class MemTable {
public:
    explicit MemTable(size_t size_threshold = 4 * 1024 * 1024);
    ~MemTable() = default;

    // Put a key-value pair. Returns previous approximate size.
    void put(const std::string& key, const std::string& value);

    // Look up a key.
    // Returns Status::OK() with value if found as PUT.
    // Returns Status::NotFound() if key not found OR found as TOMBSTONE.
    // Out-param is_tombstone can optionally indicate if key was explicitly deleted.
    Result get(const std::string& key, bool* is_tombstone = nullptr) const;

    // Mark a key as deleted (tombstone).
    void remove(const std::string& key);

    // Check if key exists in memtable (either PUT or TOMBSTONE)
    bool contains(const std::string& key) const;

    // Check if memtable has exceeded size threshold
    bool isFull() const;

    // Current approximate memory usage in bytes
    size_t approximateSize() const { return approximate_size_; }

    // Threshold in bytes
    size_t sizeThreshold() const { return size_threshold_; }
    void setSizeThreshold(size_t threshold) { size_threshold_ = threshold; }

    // Number of distinct keys stored
    size_t count() const { return table_.size(); }
    bool empty() const { return table_.empty(); }

    // Clear all entries and reset size tracking
    void clear();

    // Sorted access to entries
    const std::map<std::string, MemTableEntry>& entries() const { return table_; }

    // Estimated memory overhead per map node (pointers, color, string metadata)
    static constexpr size_t kEntryOverhead = sizeof(void*) * 4 + sizeof(MemTableEntry);

private:
    std::map<std::string, MemTableEntry> table_;
    size_t approximate_size_;
    size_t size_threshold_;

    void addSize(size_t bytes) { approximate_size_ += bytes; }
    void subSize(size_t bytes) {
        if (approximate_size_ >= bytes) {
            approximate_size_ -= bytes;
        } else {
            approximate_size_ = 0;
        }
    }
};

} // namespace lsm
