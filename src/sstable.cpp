#include "lsm/sstable.h"
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <filesystem>

namespace fs = std::filesystem;

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

static void encodeUint64LE(uint64_t value, uint8_t* buffer) {
    for (int i = 0; i < 8; ++i) {
        buffer[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
    }
}

static uint64_t decodeUint64LE(const uint8_t* buffer) {
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val |= (static_cast<uint64_t>(buffer[i]) << (8 * i));
    }
    return val;
}

// -------------------------------------------------------------
// SSTableIndex
// -------------------------------------------------------------

void SSTableIndex::add(std::string key, uint64_t offset) {
    entries_.push_back({std::move(key), offset});
}

std::vector<uint8_t> SSTableIndex::serialize() const {
    std::vector<uint8_t> buffer;
    // count (4 bytes)
    uint8_t count_buf[4];
    encodeUint32LE(static_cast<uint32_t>(entries_.size()), count_buf);
    buffer.insert(buffer.end(), count_buf, count_buf + 4);

    for (const auto& entry : entries_) {
        uint8_t klen_buf[4];
        encodeUint32LE(static_cast<uint32_t>(entry.key.size()), klen_buf);
        buffer.insert(buffer.end(), klen_buf, klen_buf + 4);
        buffer.insert(buffer.end(), entry.key.begin(), entry.key.end());

        uint8_t off_buf[8];
        encodeUint64LE(entry.offset, off_buf);
        buffer.insert(buffer.end(), off_buf, off_buf + 8);
    }

    uint32_t checksum = crc32(buffer.data(), buffer.size());
    uint8_t crc_buf[4];
    encodeUint32LE(checksum, crc_buf);
    buffer.insert(buffer.end(), crc_buf, crc_buf + 4);

    return buffer;
}

SSTableIndex SSTableIndex::deserialize(const uint8_t* data, size_t size) {
    if (size < 8) {
        throw std::runtime_error("Index block too small");
    }

    uint32_t expected_crc = decodeUint32LE(data + size - 4);
    uint32_t actual_crc = crc32(data, size - 4);
    if (expected_crc != actual_crc) {
        throw std::runtime_error("Index block CRC mismatch");
    }

    uint32_t count = decodeUint32LE(data);
    size_t cursor = 4;

    SSTableIndex index;
    index.entries_.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        if (cursor + 4 > size - 4) throw std::runtime_error("Index corrupted while reading key length");
        uint32_t klen = decodeUint32LE(data + cursor);
        cursor += 4;

        if (cursor + klen > size - 4) throw std::runtime_error("Index corrupted while reading key");
        std::string key(reinterpret_cast<const char*>(data + cursor), klen);
        cursor += klen;

        if (cursor + 8 > size - 4) throw std::runtime_error("Index corrupted while reading offset");
        uint64_t offset = decodeUint64LE(data + cursor);
        cursor += 8;

        index.entries_.push_back({std::move(key), offset});
    }

    return index;
}

void SSTableIndex::findOffsetRange(
    const std::string& key,
    uint64_t& start_offset,
    uint64_t& end_offset,
    uint64_t data_end_offset
) const {
    if (entries_.empty()) {
        start_offset = kSSTableHeaderSize;
        end_offset = data_end_offset;
        return;
    }

    // Binary search for largest index entry <= key
    // std::upper_bound finds first entry > key
    auto it = std::upper_bound(
        entries_.begin(), entries_.end(), key,
        [](const std::string& k, const IndexEntry& entry) {
            return k < entry.key;
        }
    );

    if (it == entries_.begin()) {
        // key is smaller than first indexed key
        start_offset = kSSTableHeaderSize;
        end_offset = entries_.front().offset;
    } else {
        auto prev = it - 1;
        start_offset = prev->offset;
        if (it != entries_.end()) {
            end_offset = it->offset;
        } else {
            end_offset = data_end_offset;
        }
    }
}

// -------------------------------------------------------------
// SSTableFooter
// -------------------------------------------------------------

std::vector<uint8_t> SSTableFooter::serialize() const {
    std::vector<uint8_t> buf(kSSTableFooterSize);
    encodeUint64LE(data_offset, &buf[0]);
    encodeUint64LE(data_size, &buf[8]);
    encodeUint64LE(index_offset, &buf[16]);
    encodeUint64LE(index_size, &buf[24]);
    encodeUint64LE(bloom_offset, &buf[32]);
    encodeUint64LE(bloom_size, &buf[40]);
    encodeUint32LE(record_count, &buf[48]);
    encodeUint32LE(flags, &buf[52]);
    encodeUint64LE(kSSTableFooterMagic, &buf[56]);
    return buf;
}

Status SSTableFooter::deserialize(const uint8_t* data, size_t size, SSTableFooter& out_footer) {
    if (size != kSSTableFooterSize) {
        return Status::Corruption("Invalid footer size");
    }

    uint64_t magic = decodeUint64LE(data + 56);
    if (magic != kSSTableFooterMagic) {
        return Status::Corruption("Invalid SSTable footer magic");
    }

    out_footer.data_offset = decodeUint64LE(data + 0);
    out_footer.data_size = decodeUint64LE(data + 8);
    out_footer.index_offset = decodeUint64LE(data + 16);
    out_footer.index_size = decodeUint64LE(data + 24);
    out_footer.bloom_offset = decodeUint64LE(data + 32);
    out_footer.bloom_size = decodeUint64LE(data + 40);
    out_footer.record_count = decodeUint32LE(data + 48);
    out_footer.flags = decodeUint32LE(data + 52);

    return Status::OK();
}

// -------------------------------------------------------------
// SSTableWriter
// -------------------------------------------------------------

SSTableWriter::SSTableWriter(const Options& options)
    : options_(options), is_open_(false), current_offset_(0), record_count_(0) {}

SSTableWriter::~SSTableWriter() {
    if (is_open_) {
        file_.close();
    }
}

Status SSTableWriter::open(const std::string& filepath, size_t estimated_keys) {
    filepath_ = filepath;
    file_.open(filepath_, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file_.is_open()) {
        return Status::IOError("Failed to open SSTable for writing: " + filepath_);
    }

    is_open_ = true;
    record_count_ = 0;
    last_key_.clear();

    bloom_filter_ = BloomFilter(
        estimated_keys > 0 ? estimated_keys : 100,
        options_.bloom_bits_per_key,
        options_.bloom_num_hashes
    );

    // Write Header: 8 bytes magic + 4 bytes version + 4 bytes flags
    uint8_t header[kSSTableHeaderSize];
    encodeUint64LE(kSSTableHeaderMagic, header);
    encodeUint32LE(kSSTableVersion, header + 8);
    encodeUint32LE(0, header + 12);

    file_.write(reinterpret_cast<const char*>(header), sizeof(header));
    current_offset_ = sizeof(header);

    return Status::OK();
}

Status SSTableWriter::append(const std::string& key, const std::string& value, RecordType type) {
    if (!is_open_) return Status::IOError("SSTableWriter is not open");

    if (!last_key_.empty() && key <= last_key_) {
        return Status::Corruption("SSTable keys must be strictly increasing: '" + key + "' after '" + last_key_ + "'");
    }

    bloom_filter_.add(key);

    // Add to sparse index every sparse_index_interval records (and always the first record)
    if (record_count_ % options_.sparse_index_interval == 0) {
        index_.add(key, current_offset_);
    }

    uint32_t key_len = static_cast<uint32_t>(key.size());
    uint32_t val_len = (type == RecordType::TOMBSTONE) ? 0 : static_cast<uint32_t>(value.size());

    size_t header_size = 1 + 4 + 4;
    size_t record_size = header_size + key_len + val_len;
    std::vector<uint8_t> buffer(record_size);

    buffer[0] = static_cast<uint8_t>(type);
    encodeUint32LE(key_len, &buffer[1]);
    encodeUint32LE(val_len, &buffer[5]);

    if (key_len > 0) std::memcpy(&buffer[header_size], key.data(), key_len);
    if (val_len > 0) std::memcpy(&buffer[header_size + key_len], value.data(), val_len);

    uint32_t checksum = crc32(buffer.data(), buffer.size());
    uint8_t crc_buf[4];
    encodeUint32LE(checksum, crc_buf);

    file_.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    file_.write(reinterpret_cast<const char*>(crc_buf), sizeof(crc_buf));

    current_offset_ += buffer.size() + sizeof(crc_buf);
    record_count_++;
    last_key_ = key;

    return Status::OK();
}

Status SSTableWriter::finish() {
    if (!is_open_) return Status::IOError("SSTableWriter is not open");

    uint64_t data_offset = kSSTableHeaderSize;
    uint64_t data_size = current_offset_ - data_offset;

    // Write Sparse Index
    uint64_t index_offset = current_offset_;
    std::vector<uint8_t> index_buf = index_.serialize();
    file_.write(reinterpret_cast<const char*>(index_buf.data()), index_buf.size());
    uint64_t index_size = index_buf.size();
    current_offset_ += index_size;

    // Write Bloom Filter
    uint64_t bloom_offset = current_offset_;
    std::vector<uint8_t> bloom_raw = bloom_filter_.serialize();
    // Add CRC32 to Bloom block
    uint32_t bloom_crc = crc32(bloom_raw.data(), bloom_raw.size());
    uint8_t bloom_crc_buf[4];
    encodeUint32LE(bloom_crc, bloom_crc_buf);

    file_.write(reinterpret_cast<const char*>(bloom_raw.data()), bloom_raw.size());
    file_.write(reinterpret_cast<const char*>(bloom_crc_buf), sizeof(bloom_crc_buf));
    uint64_t bloom_size = bloom_raw.size() + sizeof(bloom_crc_buf);
    current_offset_ += bloom_size;

    // Write Footer
    SSTableFooter footer;
    footer.data_offset = data_offset;
    footer.data_size = data_size;
    footer.index_offset = index_offset;
    footer.index_size = index_size;
    footer.bloom_offset = bloom_offset;
    footer.bloom_size = bloom_size;
    footer.record_count = record_count_;
    footer.flags = 0;

    std::vector<uint8_t> footer_buf = footer.serialize();
    file_.write(reinterpret_cast<const char*>(footer_buf.data()), footer_buf.size());

    file_.flush();
    file_.close();
    is_open_ = false;

    return Status::OK();
}

Status SSTableWriter::writeFromMemTable(
    const std::string& filepath,
    const MemTable& memtable,
    const Options& options
) {
    SSTableWriter writer(options);
    Status s = writer.open(filepath, memtable.count());
    if (!s.ok()) return s;

    for (const auto& [key, entry] : memtable.entries()) {
        s = writer.append(key, entry.value, entry.type);
        if (!s.ok()) return s;
    }

    return writer.finish();
}

Status SSTableWriter::writeFromEntries(
    const std::string& filepath,
    const std::vector<KeyValueEntry>& entries,
    const Options& options
) {
    SSTableWriter writer(options);
    Status s = writer.open(filepath, entries.size());
    if (!s.ok()) return s;

    for (const auto& entry : entries) {
        s = writer.append(entry.key, entry.value, entry.type);
        if (!s.ok()) return s;
    }

    return writer.finish();
}

// -------------------------------------------------------------
// SSTableReader
// -------------------------------------------------------------

SSTableReader::SSTableReader() : is_open_(false), enable_bloom_filter_(true) {}

SSTableReader::~SSTableReader() {
    close();
}

SSTableReader::SSTableReader(SSTableReader&& other) noexcept
    : filepath_(std::move(other.filepath_)),
      is_open_(other.is_open_),
      enable_bloom_filter_(other.enable_bloom_filter_),
      footer_(other.footer_),
      index_(std::move(other.index_)),
      bloom_filter_(std::move(other.bloom_filter_)) {
    other.is_open_ = false;
}

SSTableReader& SSTableReader::operator=(SSTableReader&& other) noexcept {
    if (this != &other) {
        close();
        filepath_ = std::move(other.filepath_);
        is_open_ = other.is_open_;
        enable_bloom_filter_ = other.enable_bloom_filter_;
        footer_ = other.footer_;
        index_ = std::move(other.index_);
        bloom_filter_ = std::move(other.bloom_filter_);
        other.is_open_ = false;
    }
    return *this;
}

Status SSTableReader::close() {
    is_open_ = false;
    return Status::OK();
}

Status SSTableReader::open(const std::string& filepath, bool enable_bloom_filter) {
    close();
    filepath_ = filepath;
    enable_bloom_filter_ = enable_bloom_filter;

    std::ifstream file(filepath_, std::ios::binary);
    if (!file.is_open()) {
        return Status::IOError("Failed to open SSTable: " + filepath_);
    }

    file.seekg(0, std::ios::end);
    size_t file_size = static_cast<size_t>(file.tellg());
    if (file_size < kSSTableHeaderSize + kSSTableFooterSize) {
        return Status::Corruption("SSTable file too small: " + filepath_);
    }

    // Read and verify Header
    file.seekg(0, std::ios::beg);
    uint8_t header[kSSTableHeaderSize];
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (file.gcount() < static_cast<std::streamsize>(sizeof(header))) {
        return Status::Corruption("Failed reading SSTable header");
    }

    uint64_t magic = decodeUint64LE(header);
    if (magic != kSSTableHeaderMagic) {
        return Status::Corruption("Invalid SSTable header magic number");
    }

    uint32_t version = decodeUint32LE(header + 8);
    if (version != kSSTableVersion) {
        return Status::Corruption("Unsupported SSTable version: " + std::to_string(version));
    }

    // Read and verify Footer
    file.seekg(file_size - kSSTableFooterSize, std::ios::beg);
    uint8_t footer_buf[kSSTableFooterSize];
    file.read(reinterpret_cast<char*>(footer_buf), sizeof(footer_buf));
    if (file.gcount() < static_cast<std::streamsize>(sizeof(footer_buf))) {
        return Status::Corruption("Failed reading SSTable footer");
    }

    Status s = SSTableFooter::deserialize(footer_buf, sizeof(footer_buf), footer_);
    if (!s.ok()) return s;

    // Validate offsets
    if (footer_.data_offset != kSSTableHeaderSize ||
        footer_.data_offset + footer_.data_size != footer_.index_offset ||
        footer_.index_offset + footer_.index_size != footer_.bloom_offset ||
        footer_.bloom_offset + footer_.bloom_size != file_size - kSSTableFooterSize) {
        return Status::Corruption("Corrupt SSTable offsets or sizes");
    }

    // Read and deserialize Sparse Index
    file.seekg(footer_.index_offset, std::ios::beg);
    std::vector<uint8_t> index_data(footer_.index_size);
    file.read(reinterpret_cast<char*>(index_data.data()), footer_.index_size);
    if (file.gcount() < static_cast<std::streamsize>(footer_.index_size)) {
        return Status::Corruption("Truncated index block");
    }

    try {
        index_ = SSTableIndex::deserialize(index_data.data(), index_data.size());
    } catch (const std::exception& e) {
        return Status::Corruption(std::string("Index deserialization error: ") + e.what());
    }

    // Read and deserialize Bloom Filter
    file.seekg(footer_.bloom_offset, std::ios::beg);
    std::vector<uint8_t> bloom_data(footer_.bloom_size);
    file.read(reinterpret_cast<char*>(bloom_data.data()), footer_.bloom_size);
    if (file.gcount() < static_cast<std::streamsize>(footer_.bloom_size)) {
        return Status::Corruption("Truncated Bloom filter block");
    }

    if (bloom_data.size() < 4) {
        return Status::Corruption("Corrupt Bloom filter block");
    }

    uint32_t expected_bloom_crc = decodeUint32LE(bloom_data.data() + bloom_data.size() - 4);
    uint32_t actual_bloom_crc = crc32(bloom_data.data(), bloom_data.size() - 4);
    if (expected_bloom_crc != actual_bloom_crc) {
        return Status::Corruption("Bloom filter CRC mismatch");
    }

    try {
        bloom_filter_ = BloomFilter::deserialize(bloom_data.data(), bloom_data.size() - 4);
    } catch (const std::exception& e) {
        return Status::Corruption(std::string("Bloom filter deserialization error: ") + e.what());
    }

    is_open_ = true;
    return Status::OK();
}

Result SSTableReader::get(const std::string& key, bool* is_tombstone) const {
    if (!is_open_) return Result::Error(Status::IOError("SSTableReader is not open"));

    if (enable_bloom_filter_ && !bloom_filter_.possiblyContains(key)) {
        if (is_tombstone) *is_tombstone = false;
        return Result::NotFound("Key absent according to Bloom filter");
    }

    uint64_t start_offset = 0;
    uint64_t end_offset = 0;
    index_.findOffsetRange(key, start_offset, end_offset, footer_.data_offset + footer_.data_size);

    return scanRange(start_offset, end_offset, key, is_tombstone);
}

Result SSTableReader::scanRange(
    uint64_t start_offset,
    uint64_t end_offset,
    const std::string& key,
    bool* is_tombstone
) const {
    std::ifstream file(filepath_, std::ios::binary);
    if (!file.is_open()) {
        return Result::Error(Status::IOError("Failed to open SSTable: " + filepath_));
    }

    file.seekg(start_offset, std::ios::beg);
    uint64_t curr = start_offset;

    while (curr < end_offset && file.peek() != EOF) {
        uint8_t header[9];
        file.read(reinterpret_cast<char*>(header), sizeof(header));
        if (file.gcount() < static_cast<std::streamsize>(sizeof(header))) {
            return Result::Error(Status::Corruption("Truncated record header in SSTable"));
        }

        uint8_t type_val = header[0];
        RecordType type = static_cast<RecordType>(type_val);
        uint32_t key_len = decodeUint32LE(header + 1);
        uint32_t val_len = decodeUint32LE(header + 5);

        std::string rec_key(key_len, '\0');
        if (key_len > 0) {
            file.read(&rec_key[0], key_len);
            if (file.gcount() < static_cast<std::streamsize>(key_len)) {
                return Result::Error(Status::Corruption("Truncated record key in SSTable"));
            }
        }

        std::string rec_val(val_len, '\0');
        if (val_len > 0) {
            file.read(&rec_val[0], val_len);
            if (file.gcount() < static_cast<std::streamsize>(val_len)) {
                return Result::Error(Status::Corruption("Truncated record value in SSTable"));
            }
        }

        uint8_t crc_buf[4];
        file.read(reinterpret_cast<char*>(crc_buf), sizeof(crc_buf));
        if (file.gcount() < static_cast<std::streamsize>(sizeof(crc_buf))) {
            return Result::Error(Status::Corruption("Truncated record CRC in SSTable"));
        }

        uint32_t expected_crc = decodeUint32LE(crc_buf);
        std::vector<uint8_t> buf(sizeof(header) + key_len + val_len);
        std::memcpy(buf.data(), header, sizeof(header));
        if (key_len > 0) std::memcpy(buf.data() + sizeof(header), rec_key.data(), key_len);
        if (val_len > 0) std::memcpy(buf.data() + sizeof(header) + key_len, rec_val.data(), val_len);

        if (crc32(buf.data(), buf.size()) != expected_crc) {
            return Result::Error(Status::Corruption("Record CRC mismatch in SSTable"));
        }

        curr += sizeof(header) + key_len + val_len + sizeof(crc_buf);

        if (rec_key == key) {
            if (type == RecordType::TOMBSTONE) {
                if (is_tombstone) *is_tombstone = true;
                return Result::NotFound("Key deleted (tombstone in SSTable)");
            }
            if (is_tombstone) *is_tombstone = false;
            return Result::OK(rec_val);
        }

        if (rec_key > key) {
            // Sorted order guarantee: key cannot be found further
            if (is_tombstone) *is_tombstone = false;
            return Result::NotFound("Key not found in SSTable");
        }
    }

    if (is_tombstone) *is_tombstone = false;
    return Result::NotFound("Key not found in SSTable range");
}

Status SSTableReader::readAllRecords(std::vector<KeyValueEntry>& out_records) const {
    if (!is_open_) return Status::IOError("SSTableReader is not open");

    std::ifstream file(filepath_, std::ios::binary);
    if (!file.is_open()) return Status::IOError("Failed to open SSTable: " + filepath_);

    uint64_t curr = footer_.data_offset;
    uint64_t end = footer_.data_offset + footer_.data_size;
    file.seekg(curr, std::ios::beg);

    while (curr < end && file.peek() != EOF) {
        uint8_t header[9];
        file.read(reinterpret_cast<char*>(header), sizeof(header));
        if (file.gcount() < static_cast<std::streamsize>(sizeof(header))) {
            return Status::Corruption("Truncated record header in SSTable");
        }

        RecordType type = static_cast<RecordType>(header[0]);
        uint32_t key_len = decodeUint32LE(header + 1);
        uint32_t val_len = decodeUint32LE(header + 5);

        std::string rec_key(key_len, '\0');
        if (key_len > 0) {
            file.read(&rec_key[0], key_len);
        }

        std::string rec_val(val_len, '\0');
        if (val_len > 0) {
            file.read(&rec_val[0], val_len);
        }

        uint8_t crc_buf[4];
        file.read(reinterpret_cast<char*>(crc_buf), sizeof(crc_buf));

        uint32_t expected_crc = decodeUint32LE(crc_buf);
        std::vector<uint8_t> buf(sizeof(header) + key_len + val_len);
        std::memcpy(buf.data(), header, sizeof(header));
        if (key_len > 0) std::memcpy(buf.data() + sizeof(header), rec_key.data(), key_len);
        if (val_len > 0) std::memcpy(buf.data() + sizeof(header) + key_len, rec_val.data(), val_len);

        if (crc32(buf.data(), buf.size()) != expected_crc) {
            return Status::Corruption("Record CRC mismatch in SSTable readAllRecords");
        }

        curr += sizeof(header) + key_len + val_len + sizeof(crc_buf);
        out_records.push_back(KeyValueEntry{std::move(rec_key), std::move(rec_val), type});
    }

    return Status::OK();
}

} // namespace lsm
