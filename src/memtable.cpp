#include "lsm/memtable.h"

namespace lsm {

MemTable::MemTable(size_t size_threshold)
    : approximate_size_(0), size_threshold_(size_threshold) {}

void MemTable::put(const std::string& key, const std::string& value) {
    auto it = table_.find(key);
    if (it != table_.end()) {
        // Key already exists, update size delta
        size_t old_entry_size = key.size() + (it->second.isTombstone() ? 0 : it->second.value.size()) + kEntryOverhead;
        subSize(old_entry_size);

        it->second.value = value;
        it->second.type = RecordType::PUT;

        size_t new_entry_size = key.size() + value.size() + kEntryOverhead;
        addSize(new_entry_size);
    } else {
        table_[key] = MemTableEntry{value, RecordType::PUT};
        size_t entry_size = key.size() + value.size() + kEntryOverhead;
        addSize(entry_size);
    }
}

Result MemTable::get(const std::string& key, bool* is_tombstone) const {
    auto it = table_.find(key);
    if (it == table_.end()) {
        if (is_tombstone) *is_tombstone = false;
        return Result::NotFound("Key not found in MemTable");
    }

    if (it->second.isTombstone()) {
        if (is_tombstone) *is_tombstone = true;
        return Result::NotFound("Key is deleted (tombstone)");
    }

    if (is_tombstone) *is_tombstone = false;
    return Result::OK(it->second.value);
}

void MemTable::remove(const std::string& key) {
    auto it = table_.find(key);
    if (it != table_.end()) {
        size_t old_entry_size = key.size() + (it->second.isTombstone() ? 0 : it->second.value.size()) + kEntryOverhead;
        subSize(old_entry_size);

        it->second.value.clear();
        it->second.type = RecordType::TOMBSTONE;

        size_t new_entry_size = key.size() + kEntryOverhead;
        addSize(new_entry_size);
    } else {
        table_[key] = MemTableEntry{"", RecordType::TOMBSTONE};
        size_t entry_size = key.size() + kEntryOverhead;
        addSize(entry_size);
    }
}

bool MemTable::contains(const std::string& key) const {
    return table_.find(key) != table_.end();
}

bool MemTable::isFull() const {
    return approximate_size_ >= size_threshold_;
}

void MemTable::clear() {
    table_.clear();
    approximate_size_ = 0;
}

} // namespace lsm
