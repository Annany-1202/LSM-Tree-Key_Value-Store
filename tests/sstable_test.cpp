#include "test_framework.h"
#include "lsm/sstable.h"
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

TEST(SSTableTest, CreateAndRead) {
    ScopedDir test_dir("test_sst_dir");
    std::string sst_path = test_dir.path() + "/table1.sst";

    lsm::MemTable mt;
    mt.put("cat", "meow");
    mt.put("apple", "pie");
    mt.put("dog", "bark");
    mt.put("banana", "split");
    mt.remove("cat"); // Cat is tombstone

    lsm::Options options;
    options.sparse_index_interval = 2;

    lsm::Status s = lsm::SSTableWriter::writeFromMemTable(sst_path, mt, options);
    ASSERT_TRUE(s.ok());

    lsm::SSTableReader reader;
    s = reader.open(sst_path);
    ASSERT_TRUE(s.ok());
    ASSERT_EQ(reader.recordCount(), 4);

    // apple -> pie
    lsm::Result r1 = reader.get("apple");
    ASSERT_TRUE(r1.ok());
    ASSERT_EQ(r1.value, "pie");

    // banana -> split
    lsm::Result r2 = reader.get("banana");
    ASSERT_TRUE(r2.ok());
    ASSERT_EQ(r2.value, "split");

    // dog -> bark
    lsm::Result r3 = reader.get("dog");
    ASSERT_TRUE(r3.ok());
    ASSERT_EQ(r3.value, "bark");

    // cat is tombstone -> NotFound, is_tombstone = true
    bool is_tombstone = false;
    lsm::Result r4 = reader.get("cat", &is_tombstone);
    ASSERT_FALSE(r4.ok());
    ASSERT_TRUE(is_tombstone);

    // elephant is missing
    is_tombstone = false;
    lsm::Result r5 = reader.get("elephant", &is_tombstone);
    ASSERT_FALSE(r5.ok());
    ASSERT_FALSE(is_tombstone);
}

TEST(SSTableTest, SparseIndexAndManyRecords) {
    ScopedDir test_dir("test_sst_many_dir");
    std::string sst_path = test_dir.path() + "/many.sst";

    lsm::Options options;
    options.sparse_index_interval = 8;

    lsm::SSTableWriter writer(options);
    ASSERT_TRUE(writer.open(sst_path, 200).ok());

    for (int i = 0; i < 200; ++i) {
        // Zero-padded keys for strict lexicographical sorting
        char buf[32];
        std::snprintf(buf, sizeof(buf), "key_%05d", i);
        std::string val = "val_" + std::to_string(i);
        ASSERT_TRUE(writer.append(buf, val).ok());
    }
    ASSERT_TRUE(writer.finish().ok());

    lsm::SSTableReader reader;
    ASSERT_TRUE(reader.open(sst_path).ok());
    ASSERT_EQ(reader.recordCount(), 200);

    // Verify index size is approx 200 / 8 = 25 entries
    ASSERT_TRUE(reader.index().size() >= 24 && reader.index().size() <= 26);

    // Test looking up every key
    for (int i = 0; i < 200; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "key_%05d", i);
        lsm::Result r = reader.get(buf);
        ASSERT_TRUE(r.ok());
        ASSERT_EQ(r.value, "val_" + std::to_string(i));
    }

    // Test missing keys
    ASSERT_FALSE(reader.get("key_00050_sub").ok());
    ASSERT_FALSE(reader.get("key_99999").ok());
    ASSERT_FALSE(reader.get("aaa").ok());
}

TEST(SSTableTest, InvalidHeaderAndCorruptionDetection) {
    ScopedDir test_dir("test_sst_corrupt_dir");
    std::string sst_path = test_dir.path() + "/corrupt.sst";

    {
        lsm::MemTable mt;
        mt.put("foo", "bar");
        ASSERT_TRUE(lsm::SSTableWriter::writeFromMemTable(sst_path, mt).ok());
    }

    // 1. Corrupt magic number in header
    {
        std::fstream f(sst_path, std::ios::in | std::ios::out | std::ios::binary);
        char bad_magic[8] = {'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X'};
        f.write(bad_magic, 8);
    }

    lsm::SSTableReader reader1;
    lsm::Status s1 = reader1.open(sst_path);
    ASSERT_FALSE(s1.ok());
    ASSERT_TRUE(s1.isCorruption());

    // 2. Truncate file
    {
        std::ofstream f(sst_path, std::ios::binary | std::ios::trunc);
        f.write("too_short", 9);
    }

    lsm::SSTableReader reader2;
    lsm::Status s2 = reader2.open(sst_path);
    ASSERT_FALSE(s2.ok());
    ASSERT_TRUE(s2.isCorruption());
}
