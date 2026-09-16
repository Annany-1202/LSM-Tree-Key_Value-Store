#include "test_framework.h"
#include "lsm/wal.h"
#include "lsm/memtable.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class ScopedDir {
public:
    explicit ScopedDir(std::string path) : path_(std::move(path)) {
        fs::remove_all(path_);
        fs::create_directories(path_);
    }
    ~ScopedDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const std::string& path() const { return path_; }
private:
    std::string path_;
};

TEST(WALTest, WritePutAndDelete) {
    ScopedDir test_dir("test_wal_dir");
    std::string wal_path = test_dir.path() + "/test.wal";

    {
        lsm::WAL wal;
        ASSERT_TRUE(wal.open(wal_path).ok());
        ASSERT_TRUE(wal.appendPut("key1", "val1").ok());
        ASSERT_TRUE(wal.appendPut("key2", "val2").ok());
        ASSERT_TRUE(wal.appendDelete("key1").ok());
        ASSERT_TRUE(wal.close().ok());
    }

    std::vector<lsm::WALRecord> records;
    lsm::Status s = lsm::WAL::replay(wal_path, records);
    ASSERT_TRUE(s.ok());
    ASSERT_EQ(records.size(), 3);

    ASSERT_EQ(records[0].key, "key1");
    ASSERT_EQ(records[0].value, "val1");
    ASSERT_TRUE(records[0].type == lsm::RecordType::PUT);

    ASSERT_EQ(records[1].key, "key2");
    ASSERT_EQ(records[1].value, "val2");
    ASSERT_TRUE(records[1].type == lsm::RecordType::PUT);

    ASSERT_EQ(records[2].key, "key1");
    ASSERT_TRUE(records[2].type == lsm::RecordType::TOMBSTONE);
}

TEST(WALTest, MemTableRecovery) {
    ScopedDir test_dir("test_wal_rec_dir");
    std::string wal_path = test_dir.path() + "/recovery.wal";

    {
        lsm::WAL wal;
        ASSERT_TRUE(wal.open(wal_path).ok());
        ASSERT_TRUE(wal.appendPut("alpha", "100").ok());
        ASSERT_TRUE(wal.appendPut("beta", "200").ok());
        ASSERT_TRUE(wal.appendPut("gamma", "300").ok());
        ASSERT_TRUE(wal.appendDelete("beta").ok());
        ASSERT_TRUE(wal.close().ok());
    }

    lsm::MemTable recovered_mt;
    lsm::Status s = lsm::WAL::replay(wal_path, [&](const lsm::WALRecord& rec) {
        if (rec.type == lsm::RecordType::PUT) {
            recovered_mt.put(rec.key, rec.value);
        } else if (rec.type == lsm::RecordType::TOMBSTONE) {
            recovered_mt.remove(rec.key);
        }
    });
    ASSERT_TRUE(s.ok());

    lsm::Result r1 = recovered_mt.get("alpha");
    ASSERT_TRUE(r1.ok());
    ASSERT_EQ(r1.value, "100");

    bool is_tombstone = false;
    lsm::Result r2 = recovered_mt.get("beta", &is_tombstone);
    ASSERT_FALSE(r2.ok());
    ASSERT_TRUE(is_tombstone);

    lsm::Result r3 = recovered_mt.get("gamma");
    ASSERT_TRUE(r3.ok());
    ASSERT_EQ(r3.value, "300");
}

TEST(WALTest, TruncatedRecordDetection) {
    ScopedDir test_dir("test_wal_trunc_dir");
    std::string wal_path = test_dir.path() + "/trunc.wal";

    {
        lsm::WAL wal;
        ASSERT_TRUE(wal.open(wal_path).ok());
        ASSERT_TRUE(wal.appendPut("complete_key", "complete_value").ok());
        ASSERT_TRUE(wal.close().ok());
    }

    // Now append partial bytes to simulate crash mid-write
    {
        std::ofstream out(wal_path, std::ios::binary | std::ios::app);
        uint8_t garbage[5] = {1, 10, 0, 0, 0}; // says key length is 10, but file ends
        out.write(reinterpret_cast<const char*>(garbage), sizeof(garbage));
    }

    std::vector<lsm::WALRecord> records;
    bool had_truncation = false;
    lsm::Status s = lsm::WAL::replay(wal_path, records, &had_truncation);
    ASSERT_FALSE(s.ok());
    ASSERT_TRUE(s.isCorruption());
    ASSERT_TRUE(had_truncation);
    // The first record should have been recovered before the crash point!
    ASSERT_EQ(records.size(), 1);
    ASSERT_EQ(records[0].key, "complete_key");
}

TEST(WALTest, MalformedRecordDetection) {
    ScopedDir test_dir("test_wal_malform_dir");
    std::string wal_path = test_dir.path() + "/malformed.wal";

    {
        lsm::WAL wal;
        ASSERT_TRUE(wal.open(wal_path).ok());
        ASSERT_TRUE(wal.appendPut("foo", "bar").ok());
        ASSERT_TRUE(wal.close().ok());
    }

    // Corrupt a byte in the payload
    {
        std::fstream f(wal_path, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(10, std::ios::beg); // Inside payload
        char c = 'Z';
        f.write(&c, 1);
    }

    std::vector<lsm::WALRecord> records;
    lsm::Status s = lsm::WAL::replay(wal_path, records);
    ASSERT_FALSE(s.ok());
    ASSERT_TRUE(s.isCorruption());
}
