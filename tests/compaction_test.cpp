#include "test_framework.h"
#include "lsm/compaction.h"
#include "lsm/sstable.h"
#include "lsm/memtable.h"
#include <filesystem>

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

TEST(CompactionTest, MergeAndDuplicateResolution) {
    ScopedDir test_dir("test_compaction_dir");
    std::string sst1_path = test_dir.path() + "/sst_1.sst";
    std::string sst2_path = test_dir.path() + "/sst_2.sst";
    std::string out_path = test_dir.path() + "/sst_compacted.sst";

    // Older SSTable: sst_1
    // key1 -> val1_old
    // key2 -> val2
    // key3 -> val3_old
    {
        lsm::MemTable mt;
        mt.put("key1", "val1_old");
        mt.put("key2", "val2");
        mt.put("key3", "val3_old");
        ASSERT_TRUE(lsm::SSTableWriter::writeFromMemTable(sst1_path, mt).ok());
    }

    // Newer SSTable: sst_2
    // key1 -> val1_new (update)
    // key3 -> TOMBSTONE (deleted)
    // key4 -> val4 (new insertion)
    {
        lsm::MemTable mt;
        mt.put("key1", "val1_new");
        mt.remove("key3");
        mt.put("key4", "val4");
        ASSERT_TRUE(lsm::SSTableWriter::writeFromMemTable(sst2_path, mt).ok());
    }

    // Compact sst_1 (older) and sst_2 (newer) into sst_compacted
    std::vector<std::string> inputs = {sst1_path, sst2_path};
    lsm::Status s = lsm::Compaction::compact(inputs, out_path, lsm::Options{}, true);
    ASSERT_TRUE(s.ok());

    // Verify old files were removed
    ASSERT_FALSE(fs::exists(sst1_path));
    ASSERT_FALSE(fs::exists(sst2_path));
    ASSERT_TRUE(fs::exists(out_path));

    // Verify compacted SSTable contents
    lsm::SSTableReader reader;
    ASSERT_TRUE(reader.open(out_path).ok());
    ASSERT_EQ(reader.recordCount(), 3); // key1, key2, key4 (key3 deleted by tombstone)

    // key1 must have newer value
    lsm::Result r1 = reader.get("key1");
    ASSERT_TRUE(r1.ok());
    ASSERT_EQ(r1.value, "val1_new");

    // key2 preserved
    lsm::Result r2 = reader.get("key2");
    ASSERT_TRUE(r2.ok());
    ASSERT_EQ(r2.value, "val2");

    // key3 was deleted
    lsm::Result r3 = reader.get("key3");
    ASSERT_FALSE(r3.ok());

    // key4 present
    lsm::Result r4 = reader.get("key4");
    ASSERT_TRUE(r4.ok());
    ASSERT_EQ(r4.value, "val4");
}
