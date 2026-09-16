#include "test_framework.h"
#include "lsm/kv_store.h"
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

class ScopedDBCleaner {
public:
    explicit ScopedDBCleaner(std::string path) : path_(std::move(path)) {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    ~ScopedDBCleaner() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const std::string& path() const { return path_; }
private:
    std::string path_;
};

TEST(IntegrationTest, BasicPutGetDelete) {
    ScopedDBCleaner db("test_db_basic");
    lsm::KVStore store;
    ASSERT_TRUE(store.open(db.path()).ok());

    ASSERT_TRUE(store.put("key1", "val1").ok());
    ASSERT_TRUE(store.put("key2", "val2").ok());

    lsm::Result r1 = store.get("key1");
    ASSERT_TRUE(r1.ok());
    ASSERT_EQ(r1.value, "val1");

    ASSERT_TRUE(store.remove("key1").ok());

    lsm::Result r1_del = store.get("key1");
    ASSERT_FALSE(r1_del.ok());
    ASSERT_TRUE(r1_del.status.isNotFound());

    // key2 should still exist
    lsm::Result r2 = store.get("key2");
    ASSERT_TRUE(r2.ok());
    ASSERT_EQ(r2.value, "val2");

    ASSERT_TRUE(store.close().ok());
}

TEST(IntegrationTest, EndToEndSequence1_PutDeleteFlushRestart) {
    // Sequence 1: PUT -> GET -> DELETE -> flush -> restart -> GET
    ScopedDBCleaner db("test_db_seq1");
    {
        lsm::KVStore store;
        ASSERT_TRUE(store.open(db.path()).ok());
        ASSERT_TRUE(store.put("user1", "Alice").ok());

        lsm::Result r1 = store.get("user1");
        ASSERT_TRUE(r1.ok());
        ASSERT_EQ(r1.value, "Alice");

        ASSERT_TRUE(store.remove("user1").ok());
        ASSERT_TRUE(store.flush().ok());
        ASSERT_TRUE(store.close().ok());
    }

    // Reopen database
    {
        lsm::KVStore store;
        ASSERT_TRUE(store.open(db.path()).ok());
        lsm::Result r_after = store.get("user1");
        ASSERT_FALSE(r_after.ok());
        ASSERT_TRUE(r_after.status.isNotFound());
        ASSERT_TRUE(store.close().ok());
    }
}

TEST(IntegrationTest, EndToEndSequence2_PutFlushUpdateFlushGet) {
    // Sequence 2: PUT -> flush -> PUT updated value -> flush -> GET
    ScopedDBCleaner db("test_db_seq2");
    {
        lsm::KVStore store;
        ASSERT_TRUE(store.open(db.path()).ok());
        ASSERT_TRUE(store.put("item", "version1").ok());
        ASSERT_TRUE(store.flush().ok());

        ASSERT_TRUE(store.put("item", "version2").ok());
        ASSERT_TRUE(store.flush().ok());

        lsm::Result r = store.get("item");
        ASSERT_TRUE(r.ok());
        ASSERT_EQ(r.value, "version2");
        ASSERT_TRUE(store.close().ok());
    }

    // Restart and verify
    {
        lsm::KVStore store;
        ASSERT_TRUE(store.open(db.path()).ok());
        lsm::Result r = store.get("item");
        ASSERT_TRUE(r.ok());
        ASSERT_EQ(r.value, "version2");
        ASSERT_TRUE(store.close().ok());
    }
}

TEST(IntegrationTest, EndToEndSequence3_PutFlushDeleteFlushGet) {
    // Sequence 3: PUT -> flush -> DELETE -> flush -> GET
    ScopedDBCleaner db("test_db_seq3");
    {
        lsm::KVStore store;
        ASSERT_TRUE(store.open(db.path()).ok());
        ASSERT_TRUE(store.put("item", "to_delete").ok());
        ASSERT_TRUE(store.flush().ok());

        ASSERT_TRUE(store.remove("item").ok());
        ASSERT_TRUE(store.flush().ok());

        lsm::Result r = store.get("item");
        ASSERT_FALSE(r.ok());
        ASSERT_TRUE(r.status.isNotFound());
        ASSERT_TRUE(store.close().ok());
    }

    // Restart and verify
    {
        lsm::KVStore store;
        ASSERT_TRUE(store.open(db.path()).ok());
        lsm::Result r = store.get("item");
        ASSERT_FALSE(r.ok());
        ASSERT_TRUE(r.status.isNotFound());
        ASSERT_TRUE(store.close().ok());
    }
}

TEST(IntegrationTest, RestartPersistenceWithWALAndSSTable) {
    ScopedDBCleaner db("test_db_persist");
    {
        lsm::KVStore store;
        ASSERT_TRUE(store.open(db.path()).ok());

        // SSTable 1 data
        ASSERT_TRUE(store.put("flushed1", "data1").ok());
        ASSERT_TRUE(store.put("flushed2", "data2").ok());
        ASSERT_TRUE(store.flush().ok());

        // Unflushed MemTable data (resides in WAL upon closing)
        ASSERT_TRUE(store.put("unflushed1", "wal_data1").ok());
        ASSERT_TRUE(store.put("unflushed2", "wal_data2").ok());
        ASSERT_TRUE(store.remove("flushed1").ok()); // unflushed tombstone in WAL overriding SSTable!

        ASSERT_TRUE(store.close().ok());
    }

    // Process restart: open new store instance
    {
        lsm::KVStore store;
        ASSERT_TRUE(store.open(db.path()).ok());

        // flushed1 was deleted by unflushed tombstone
        lsm::Result r_del = store.get("flushed1");
        ASSERT_FALSE(r_del.ok());
        ASSERT_TRUE(r_del.status.isNotFound());

        // flushed2 still exists from SSTable
        lsm::Result r_f2 = store.get("flushed2");
        ASSERT_TRUE(r_f2.ok());
        ASSERT_EQ(r_f2.value, "data2");

        // unflushed1 and unflushed2 recovered from WAL
        lsm::Result r_u1 = store.get("unflushed1");
        ASSERT_TRUE(r_u1.ok());
        ASSERT_EQ(r_u1.value, "wal_data1");

        lsm::Result r_u2 = store.get("unflushed2");
        ASSERT_TRUE(r_u2.ok());
        ASSERT_EQ(r_u2.value, "wal_data2");

        ASSERT_TRUE(store.close().ok());
    }
}

TEST(IntegrationTest, AutomaticCompactionOnThreshold) {
    ScopedDBCleaner db("test_db_compaction");

    lsm::Options options;
    options.compaction_threshold = 3; // Trigger compaction when 3 SSTables exist

    lsm::KVStore store(options);
    ASSERT_TRUE(store.open(db.path()).ok());

    // Flush 1
    ASSERT_TRUE(store.put("k1", "v1").ok());
    ASSERT_TRUE(store.put("common", "old_v").ok());
    ASSERT_TRUE(store.flush().ok());
    ASSERT_EQ(store.sstableCount(), 1);

    // Flush 2
    ASSERT_TRUE(store.put("k2", "v2").ok());
    ASSERT_TRUE(store.flush().ok());
    ASSERT_EQ(store.sstableCount(), 2);

    // Flush 3: should trigger automatic compaction!
    ASSERT_TRUE(store.put("common", "new_v").ok()); // update
    ASSERT_TRUE(store.remove("k1").ok()); // delete
    ASSERT_TRUE(store.flush().ok());

    // Compaction should have merged 3 SSTables into 1!
    ASSERT_EQ(store.sstableCount(), 1);

    // Verify all keys
    ASSERT_FALSE(store.get("k1").ok()); // deleted
    ASSERT_EQ(store.get("k2").value, "v2");
    ASSERT_EQ(store.get("common").value, "new_v"); // updated

    ASSERT_TRUE(store.close().ok());

    // Verify persistence after compaction
    {
        lsm::KVStore reopened(options);
        ASSERT_TRUE(reopened.open(db.path()).ok());
        ASSERT_EQ(reopened.sstableCount(), 1);
        ASSERT_FALSE(reopened.get("k1").ok());
        ASSERT_EQ(reopened.get("k2").value, "v2");
        ASSERT_EQ(reopened.get("common").value, "new_v");
        ASSERT_TRUE(reopened.close().ok());
    }
}

TEST(IntegrationTest, InvalidArgumentsAndErrorHandling) {
    ScopedDBCleaner db("test_db_err");
    lsm::KVStore store;

    // Put before open
    ASSERT_FALSE(store.put("k", "v").ok());
    ASSERT_FALSE(store.get("k").ok());

    ASSERT_TRUE(store.open(db.path()).ok());

    // Empty key checks
    ASSERT_FALSE(store.put("", "v").ok());
    ASSERT_FALSE(store.get("").ok());
    ASSERT_FALSE(store.remove("").ok());

    ASSERT_TRUE(store.close().ok());
}
