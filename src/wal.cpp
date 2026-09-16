#include "lsm/wal.h"
#include <cstring>

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

WAL::WAL() : is_open_(false) {}

WAL::~WAL() {
    close();
}

Status WAL::open(const std::string& filepath) {
    close();
    filepath_ = filepath;
    file_.open(filepath_, std::ios::binary | std::ios::out | std::ios::app);
    if (!file_.is_open()) {
        return Status::IOError("Failed to open WAL file: " + filepath_);
    }
    is_open_ = true;
    return Status::OK();
}

Status WAL::close() {
    if (is_open_) {
        file_.flush();
        file_.close();
        is_open_ = false;
    }
    return Status::OK();
}

Status WAL::appendPut(const std::string& key, const std::string& value) {
    return appendRecord(RecordType::PUT, key, value);
}

Status WAL::appendDelete(const std::string& key) {
    return appendRecord(RecordType::TOMBSTONE, key, "");
}

Status WAL::appendRecord(RecordType type, const std::string& key, const std::string& value) {
    if (!is_open_) {
        return Status::IOError("WAL is not open");
    }

    uint32_t key_len = static_cast<uint32_t>(key.size());
    uint32_t val_len = (type == RecordType::TOMBSTONE) ? 0 : static_cast<uint32_t>(value.size());

    // Buffer: 1 byte type + 4 bytes key_len + 4 bytes val_len + key + val
    size_t header_size = 1 + 4 + 4;
    size_t payload_size = key_len + val_len;
    size_t total_record_size = header_size + payload_size;

    std::vector<uint8_t> buffer(total_record_size);
    buffer[0] = static_cast<uint8_t>(type);
    encodeUint32LE(key_len, &buffer[1]);
    encodeUint32LE(val_len, &buffer[5]);

    if (key_len > 0) {
        std::memcpy(&buffer[header_size], key.data(), key_len);
    }
    if (val_len > 0) {
        std::memcpy(&buffer[header_size + key_len], value.data(), val_len);
    }

    uint32_t checksum = crc32(buffer.data(), buffer.size());
    uint8_t crc_buf[4];
    encodeUint32LE(checksum, crc_buf);

    file_.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    file_.write(reinterpret_cast<const char*>(crc_buf), sizeof(crc_buf));

    if (!file_.good()) {
        return Status::IOError("Failed writing to WAL file");
    }

    // Immediate flush for synchronous durability
    file_.flush();
    return Status::OK();
}

Status WAL::flush() {
    if (!is_open_) return Status::IOError("WAL is not open");
    file_.flush();
    if (!file_.good()) {
        return Status::IOError("Failed to flush WAL file");
    }
    return Status::OK();
}

Status WAL::clear() {
    close();
    file_.open(filepath_, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file_.is_open()) {
        return Status::IOError("Failed to truncate WAL file: " + filepath_);
    }
    is_open_ = true;
    return Status::OK();
}

Status WAL::replay(
    const std::string& filepath,
    std::vector<WALRecord>& out_records,
    bool* out_had_truncation
) {
    return replay(filepath, [&](const WALRecord& rec) {
        out_records.push_back(rec);
    }, out_had_truncation);
}

Status WAL::replay(
    const std::string& filepath,
    const std::function<void(const WALRecord&)>& consumer,
    bool* out_had_truncation
) {
    if (out_had_truncation) {
        *out_had_truncation = false;
    }

    std::ifstream in(filepath, std::ios::binary);
    if (!in.is_open()) {
        // WAL file might not exist yet if fresh database
        return Status::OK();
    }

    // Check if file is empty
    in.seekg(0, std::ios::end);
    std::streamsize file_size = in.tellg();
    if (file_size <= 0) {
        return Status::OK();
    }
    in.seekg(0, std::ios::beg);

    while (in.peek() != EOF) {
        // Read header: 1 byte type + 4 bytes key_len + 4 bytes val_len
        uint8_t header[9];
        in.read(reinterpret_cast<char*>(header), sizeof(header));
        std::streamsize bytes_read = in.gcount();

        if (bytes_read == 0) {
            break; // Clean EOF
        }

        if (bytes_read < static_cast<std::streamsize>(sizeof(header))) {
            // Truncated header
            if (out_had_truncation) *out_had_truncation = true;
            return Status::Corruption("Truncated WAL record header");
        }

        uint8_t type_val = header[0];
        if (type_val != static_cast<uint8_t>(RecordType::PUT) &&
            type_val != static_cast<uint8_t>(RecordType::TOMBSTONE)) {
            return Status::Corruption("Invalid RecordType in WAL: " + std::to_string(type_val));
        }
        RecordType type = static_cast<RecordType>(type_val);

        uint32_t key_len = decodeUint32LE(&header[1]);
        uint32_t val_len = decodeUint32LE(&header[5]);

        // Defensive checks against absurd sizes
        if (key_len > 16 * 1024 * 1024 || val_len > 64 * 1024 * 1024) {
            return Status::Corruption("WAL record payload lengths exceed reasonable safety limits");
        }

        std::string key;
        key.resize(key_len);
        if (key_len > 0) {
            in.read(&key[0], key_len);
            if (in.gcount() < static_cast<std::streamsize>(key_len)) {
                if (out_had_truncation) *out_had_truncation = true;
                return Status::Corruption("Truncated WAL record key payload");
            }
        }

        std::string value;
        if (val_len > 0) {
            value.resize(val_len);
            in.read(&value[0], val_len);
            if (in.gcount() < static_cast<std::streamsize>(val_len)) {
                if (out_had_truncation) *out_had_truncation = true;
                return Status::Corruption("Truncated WAL record value payload");
            }
        }

        // Read 4-byte CRC32
        uint8_t crc_buf[4];
        in.read(reinterpret_cast<char*>(crc_buf), sizeof(crc_buf));
        if (in.gcount() < static_cast<std::streamsize>(sizeof(crc_buf))) {
            if (out_had_truncation) *out_had_truncation = true;
            return Status::Corruption("Truncated WAL record checksum");
        }
        uint32_t expected_crc = decodeUint32LE(crc_buf);

        // Calculate CRC
        std::vector<uint8_t> buffer(sizeof(header) + key_len + val_len);
        std::memcpy(buffer.data(), header, sizeof(header));
        if (key_len > 0) {
            std::memcpy(buffer.data() + sizeof(header), key.data(), key_len);
        }
        if (val_len > 0) {
            std::memcpy(buffer.data() + sizeof(header) + key_len, value.data(), val_len);
        }

        uint32_t actual_crc = crc32(buffer.data(), buffer.size());
        if (actual_crc != expected_crc) {
            return Status::Corruption("WAL record checksum mismatch");
        }

        // Valid record
        consumer(WALRecord{type, std::move(key), std::move(value)});
    }

    return Status::OK();
}

} // namespace lsm
