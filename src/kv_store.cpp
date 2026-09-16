#include "lsm/kv_store.h"
#include "lsm/memtable.h"
#include "lsm/wal.h"
#include "lsm/sstable.h"
#include "lsm/compaction.h"
#include <vector>
#include <memory>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace lsm {

class KVStoreImpl {
public:
    explicit KVStoreImpl(const Options& options)
        : options_(options),
          memtable_(options.memtable_size_threshold),
          is_open_(false),
          next_sst_seq_(1) {}

    ~KVStoreImpl() {
        close();
    }

    Status open(const std::string& path) {
        if (is_open_) {
            close();
        }

        if (path.empty()) {
            return Status::InvalidArgument("Database path cannot be empty");
        }

        db_path_ = path;

        // Ensure directory exists
        std::error_code ec;
        if (!fs::exists(db_path_)) {
            if (!fs::create_directories(db_path_, ec)) {
                return Status::IOError("Failed to create database directory: " + ec.message());
            }
        }

        // 1. Read Manifest if present
        manifest_path_ = db_path_ + "/manifest.txt";
        std::vector<std::string> active_sst_filenames;
        if (fs::exists(manifest_path_)) {
            std::ifstream mf(manifest_path_);
            std::string line;
            while (std::getline(mf, line)) {
                if (!line.empty()) {
                    active_sst_filenames.push_back(line);
                }
            }
        }

        // Load active SSTables
        sstables_.clear();
        uint64_t max_seq = 0;
        for (const auto& fname : active_sst_filenames) {
            std::string full_path = db_path_ + "/" + fname;
            if (fs::exists(full_path)) {
                auto reader = std::make_unique<SSTableReader>();
                Status s = reader->open(full_path, options_.enable_bloom_filter);
                if (!s.ok()) {
                    return Status::Corruption("Failed opening SSTable listed in manifest: " + s.message());
                }
                sstables_.push_back(std::move(reader));

                // Extract sequence number if formatted like sst_000001.sst
                if (fname.rfind("sst_", 0) == 0) {
                    try {
                        std::string num_part = fname.substr(4, fname.find('.') - 4);
                        uint64_t seq = std::stoull(num_part);
                        if (seq > max_seq) max_seq = seq;
                    } catch (...) {}
                }
            }
        }
        next_sst_seq_ = max_seq + 1;

        // 2. Open WAL & Replay unflushed mutations into MemTable
        wal_path_ = db_path_ + "/wal.log";
        memtable_.clear();

        if (fs::exists(wal_path_)) {
            bool had_truncation = false;
            Status s = WAL::replay(wal_path_, [&](const WALRecord& rec) {
                if (rec.type == RecordType::PUT) {
                    memtable_.put(rec.key, rec.value);
                } else if (rec.type == RecordType::TOMBSTONE) {
                    memtable_.remove(rec.key);
                }
            }, &had_truncation);

            if (!s.ok() && !had_truncation) {
                return Status::Corruption("WAL replay error: " + s.message());
            }
        }

        // Open WAL for active appending
        Status s = wal_.open(wal_path_);
        if (!s.ok()) {
            return s;
        }

        is_open_ = true;
        return Status::OK();
    }

    Status close() {
        if (!is_open_) return Status::OK();

        wal_.close();
        sstables_.clear();
        memtable_.clear();
        is_open_ = false;
        return Status::OK();
    }

    Status put(const std::string& key, const std::string& value) {
        if (!is_open_) return Status::IOError("Database is not open");
        if (key.empty()) return Status::InvalidArgument("Key cannot be empty");

        // Flush if MemTable full
        if (memtable_.isFull()) {
            Status s = flushInternal();
            if (!s.ok()) return s;
        }

        // 1. Write WAL
        Status s = wal_.appendPut(key, value);
        if (!s.ok()) return s;

        // 2. Update MemTable
        memtable_.put(key, value);
        return Status::OK();
    }

    Result get(const std::string& key) {
        if (!is_open_) return Result::Error(Status::IOError("Database is not open"));
        if (key.empty()) return Result::Error(Status::InvalidArgument("Key cannot be empty"));

        // Step 1: Check MemTable
        bool is_tombstone = false;
        Result r = memtable_.get(key, &is_tombstone);
        if (r.ok()) {
            return r;
        }
        if (is_tombstone) {
            return Result::NotFound("Key was deleted");
        }

        // Step 2: Check SSTables from newest to oldest
        for (auto it = sstables_.rbegin(); it != sstables_.rend(); ++it) {
            is_tombstone = false;
            r = (*it)->get(key, &is_tombstone);
            if (r.ok()) {
                return r;
            }
            if (is_tombstone) {
                return Result::NotFound("Key was deleted in SSTable");
            }
        }

        return Result::NotFound("Key not found in KVStore");
    }

    Status remove(const std::string& key) {
        if (!is_open_) return Status::IOError("Database is not open");
        if (key.empty()) return Status::InvalidArgument("Key cannot be empty");

        if (memtable_.isFull()) {
            Status s = flushInternal();
            if (!s.ok()) return s;
        }

        // 1. Write WAL tombstone
        Status s = wal_.appendDelete(key);
        if (!s.ok()) return s;

        // 2. Insert tombstone into MemTable
        memtable_.remove(key);
        return Status::OK();
    }

    Status flush() {
        if (!is_open_) return Status::IOError("Database is not open");
        return flushInternal();
    }

    Status compact() {
        if (!is_open_) return Status::IOError("Database is not open");
        return compactInternal();
    }

    size_t sstableCount() const {
        return sstables_.size();
    }

    size_t memtableSize() const {
        return memtable_.approximateSize();
    }

    const Options& options() const {
        return options_;
    }

private:
    Options options_;
    MemTable memtable_;
    WAL wal_;
    std::vector<std::unique_ptr<SSTableReader>> sstables_;
    bool is_open_;
    std::string db_path_;
    std::string manifest_path_;
    std::string wal_path_;
    uint64_t next_sst_seq_;

    std::string generateSSTableFilename() {
        std::ostringstream ss;
        ss << "sst_" << std::setw(6) << std::setfill('0') << next_sst_seq_++ << ".sst";
        return ss.str();
    }

    Status updateManifest() {
        std::string temp_manifest = manifest_path_ + ".tmp";
        std::ofstream mf(temp_manifest, std::ios::trunc);
        if (!mf.is_open()) {
            return Status::IOError("Failed to open temporary manifest for writing");
        }

        for (const auto& reader : sstables_) {
            std::string filename = fs::path(reader->filepath()).filename().string();
            mf << filename << "\n";
        }
        mf.flush();
        mf.close();

        std::error_code ec;
        fs::rename(temp_manifest, manifest_path_, ec);
        if (ec) {
            fs::remove(temp_manifest, ec);
            return Status::IOError("Failed to update manifest: " + ec.message());
        }

        return Status::OK();
    }

    Status flushInternal() {
        if (memtable_.empty()) {
            return Status::OK();
        }

        std::string sst_filename = generateSSTableFilename();
        std::string sst_path = db_path_ + "/" + sst_filename;

        // Write MemTable to new SSTable
        Status s = SSTableWriter::writeFromMemTable(sst_path, memtable_, options_);
        if (!s.ok()) {
            return s;
        }

        // Open reader for the new SSTable
        auto reader = std::make_unique<SSTableReader>();
        s = reader->open(sst_path, options_.enable_bloom_filter);
        if (!s.ok()) {
            return s;
        }
        sstables_.push_back(std::move(reader));

        // Update manifest
        s = updateManifest();
        if (!s.ok()) {
            return s;
        }

        // Reset MemTable & WAL
        memtable_.clear();
        s = wal_.clear();
        if (!s.ok()) {
            return s;
        }

        // Trigger compaction if threshold reached
        if (sstables_.size() >= options_.compaction_threshold) {
            s = compactInternal();
            if (!s.ok()) {
                return s;
            }
        }

        return Status::OK();
    }

    Status compactInternal() {
        if (sstables_.size() < 2) {
            return Status::OK();
        }

        std::vector<std::string> input_paths;
        for (const auto& reader : sstables_) {
            input_paths.push_back(reader->filepath());
        }

        std::string compacted_filename = generateSSTableFilename();
        std::string compacted_path = db_path_ + "/" + compacted_filename;

        // Close current readers before compaction deletes them
        for (auto& reader : sstables_) {
            reader->close();
        }
        sstables_.clear();

        Status s = Compaction::compact(input_paths, compacted_path, options_, true);
        if (!s.ok()) {
            // Re-open old SSTables if compaction failed
            for (const auto& p : input_paths) {
                if (fs::exists(p)) {
                    auto r = std::make_unique<SSTableReader>();
                    if (r->open(p, options_.enable_bloom_filter).ok()) {
                        sstables_.push_back(std::move(r));
                    }
                }
            }
            return s;
        }

        // Open newly compacted SSTable
        auto new_reader = std::make_unique<SSTableReader>();
        s = new_reader->open(compacted_path, options_.enable_bloom_filter);
        if (!s.ok()) {
            return s;
        }
        sstables_.push_back(std::move(new_reader));

        // Update manifest to only reference the compacted SSTable
        return updateManifest();
    }
};

KVStore::KVStore() : impl_(std::make_unique<KVStoreImpl>(Options{})) {}
KVStore::KVStore(const Options& options) : impl_(std::make_unique<KVStoreImpl>(options)) {}
KVStore::~KVStore() = default;
KVStore::KVStore(KVStore&&) noexcept = default;
KVStore& KVStore::operator=(KVStore&&) noexcept = default;

Status KVStore::open(const std::string& path) { return impl_->open(path); }
Status KVStore::close() { return impl_->close(); }
Status KVStore::put(const std::string& key, const std::string& value) { return impl_->put(key, value); }
Result KVStore::get(const std::string& key) { return impl_->get(key); }
Status KVStore::remove(const std::string& key) { return impl_->remove(key); }
Status KVStore::flush() { return impl_->flush(); }
Status KVStore::compact() { return impl_->compact(); }
size_t KVStore::sstableCount() const { return impl_->sstableCount(); }
size_t KVStore::memtableSize() const { return impl_->memtableSize(); }
const Options& KVStore::options() const { return impl_->options(); }

} // namespace lsm
