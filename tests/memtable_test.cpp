#include "test_framework.h"
#include "lsm/memtable.h"

TEST(MemTableTest, InsertAndLookup) {
    lsm::MemTable mt;
    mt.put("key1", "val1");
    mt.put("key2", "val2");

    lsm::Result r1 = mt.get("key1");
    ASSERT_TRUE(r1.ok());
    ASSERT_EQ(r1.value, "val1");

    lsm::Result r2 = mt.get("key2");
    ASSERT_TRUE(r2.ok());
    ASSERT_EQ(r2.value, "val2");

    lsm::Result r3 = mt.get("key3");
    ASSERT_FALSE(r3.ok());
    ASSERT_TRUE(r3.status.isNotFound());
}

TEST(MemTableTest, Update) {
    lsm::MemTable mt;
    mt.put("key1", "initial");
    ASSERT_EQ(mt.get("key1").value, "initial");

    mt.put("key1", "updated");
    ASSERT_EQ(mt.get("key1").value, "updated");
    ASSERT_EQ(mt.count(), 1);
}

TEST(MemTableTest, DeleteAndTombstone) {
    lsm::MemTable mt;
    mt.put("user1", "alice");
    ASSERT_TRUE(mt.get("user1").ok());

    bool is_tombstone = false;
    mt.remove("user1");
    lsm::Result r = mt.get("user1", &is_tombstone);
    ASSERT_FALSE(r.ok());
    ASSERT_TRUE(r.status.isNotFound());
    ASSERT_TRUE(is_tombstone);

    // Deleting a nonexistent key should still record a tombstone
    mt.remove("user2");
    is_tombstone = false;
    lsm::Result r2 = mt.get("user2", &is_tombstone);
    ASSERT_FALSE(r2.ok());
    ASSERT_TRUE(is_tombstone);
}

TEST(MemTableTest, SortedIteration) {
    lsm::MemTable mt;
    mt.put("orange", "1");
    mt.put("apple", "2");
    mt.put("banana", "3");
    mt.put("cherry", "4");

    std::vector<std::string> keys;
    for (const auto& [k, v] : mt.entries()) {
        keys.push_back(k);
    }

    std::vector<std::string> expected = {"apple", "banana", "cherry", "orange"};
    ASSERT_EQ(keys.size(), expected.size());
    for (size_t i = 0; i < keys.size(); ++i) {
        ASSERT_EQ(keys[i], expected[i]);
    }
}

TEST(MemTableTest, SizeTrackingAndFlushThreshold) {
    // 500 bytes threshold
    lsm::MemTable mt(500);
    ASSERT_EQ(mt.approximateSize(), 0);
    ASSERT_FALSE(mt.isFull());

    std::string large_val(100, 'x');
    mt.put("k1", large_val);
    ASSERT_TRUE(mt.approximateSize() > 100);
    ASSERT_FALSE(mt.isFull());

    mt.put("k2", large_val);
    mt.put("k3", large_val);
    mt.put("k4", large_val);
    mt.put("k5", large_val);

    ASSERT_TRUE(mt.isFull());

    // Test clear resets size
    mt.clear();
    ASSERT_EQ(mt.approximateSize(), 0);
    ASSERT_EQ(mt.count(), 0);
    ASSERT_FALSE(mt.isFull());
}
