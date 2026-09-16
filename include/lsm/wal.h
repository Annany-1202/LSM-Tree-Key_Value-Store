#pragma once

#include "lsm/types.h"
#include "lsm/status.h"
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <functional>

namespace lsm {

struct WALRecord {
    RecordType type;
    std::string key;
    std::string value;
};

class WAL {
public:
    WAL();
    ~WAL();

    // Prevent copying
    WAL(const WAL&) = delete;
    WAL& operator=(const WAL&) = delete;

    // Open/create a WAL file at the given path in append mode
    Status open(const std::string& filepath);

    // Close WAL file
    Status close();

    // Append PUT record
    Status appendPut(const std::string& key, const std::string& value);

    // Append DELETE record
    Status appendDelete(const std::string& key);

    // Flush any buffered WAL writes to disk
    Status flush();

    // Truncate/clear the WAL file (used after a successful MemTable flush to SSTable)
    Status clear();

    // Replay all records from a WAL file at filepath.
    // If stop_on_corruption is true, replay stops and returns OK or warning when hitting
    // a truncated tail record, while collecting all valid records up to that point.
    // Returns Status::Corruption() or Status::IOError() if a fatal error occurs.
    static Status replay(
        const std::string& filepath,
        std::vector<WALRecord>& out_records,
        bool* out_had_truncation = nullptr
    );

    // Helper: replay directly into a consumer callback (e.g. MemTable reconstructor)
    static Status replay(
        const std::string& filepath,
        const std::function<void(const WALRecord&)>& consumer,
        bool* out_had_truncation = nullptr
    );

    const std::string& filepath() const { return filepath_; }
    bool isOpen() const { return is_open_; }

private:
    std::string filepath_;
    std::ofstream file_;
    bool is_open_;

    Status appendRecord(RecordType type, const std::string& key, const std::string& value);
};

} // namespace lsm
