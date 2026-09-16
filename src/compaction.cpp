#include "lsm/compaction.h"
#include <map>
#include <filesystem>

namespace fs = std::filesystem;

namespace lsm {

Status Compaction::compact(
    const std::vector<std::string>& input_sstable_paths,
    const std::string& out_sstable_path,
    const Options& options,
    bool remove_old
) {
    if (input_sstable_paths.empty()) {
        return Status::InvalidArgument("No SSTables provided for compaction");
    }

    // Merge entries in chronological order (oldest to newest).
    // std::map maintains sorted key order and later assignments overwrite earlier ones.
    std::map<std::string, KeyValueEntry> merged;

    for (const auto& sst_path : input_sstable_paths) {
        SSTableReader reader;
        Status s = reader.open(sst_path, false); // Don't need bloom filter during full scan
        if (!s.ok()) {
            return s;
        }

        std::vector<KeyValueEntry> records;
        s = reader.readAllRecords(records);
        if (!s.ok()) {
            return s;
        }

        for (auto& rec : records) {
            merged[rec.key] = std::move(rec);
        }
    }

    // Collect surviving entries (tombstones are purged in full compaction)
    std::vector<KeyValueEntry> surviving_entries;
    surviving_entries.reserve(merged.size());

    for (auto& [key, entry] : merged) {
        if (!entry.isTombstone()) {
            surviving_entries.push_back(std::move(entry));
        }
    }

    // Write to temporary file first for safe replacement
    std::string temp_path = out_sstable_path + ".compact_tmp";
    std::error_code ec;
    fs::remove(temp_path, ec);

    Status s = SSTableWriter::writeFromEntries(temp_path, surviving_entries, options);
    if (!s.ok()) {
        fs::remove(temp_path, ec);
        return s;
    }

    // Verify that the new SSTable can be cleanly opened and read
    {
        SSTableReader verifier;
        s = verifier.open(temp_path);
        if (!s.ok()) {
            fs::remove(temp_path, ec);
            return Status::Corruption("Failed to verify compacted SSTable: " + s.message());
        }
        verifier.close();
    }

    // Safely replace target SSTable
    fs::rename(temp_path, out_sstable_path, ec);
    if (ec) {
        fs::remove(temp_path, ec);
        return Status::IOError("Failed to rename temporary compacted SSTable: " + ec.message());
    }

    // Old SSTables are only deleted after the new one is completely valid and in place
    if (remove_old) {
        for (const auto& old_path : input_sstable_paths) {
            if (old_path != out_sstable_path) {
                fs::remove(old_path, ec);
            }
        }
    }

    return Status::OK();
}

} // namespace lsm
