#include "test_framework.h"
#include "lsm/status.h"
#include "lsm/kv_store.h"

TEST(BasicTest, StatusAndResult) {
    lsm::Status s = lsm::Status::OK();
    ASSERT_TRUE(s.ok());
    ASSERT_FALSE(s.isNotFound());

    lsm::Status nf = lsm::Status::NotFound("missing");
    ASSERT_FALSE(nf.ok());
    ASSERT_TRUE(nf.isNotFound());
    ASSERT_EQ(nf.message(), "missing");

    lsm::Result r = lsm::Result::OK("hello");
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.value, "hello");
}

TEST(BasicTest, KVStoreSkeleton) {
    lsm::KVStore store;
    lsm::Status s = store.open("test_db");
    ASSERT_TRUE(s.ok());

    lsm::Result r = store.get("non_existent_key");
    ASSERT_FALSE(r.ok());
    ASSERT_TRUE(r.status.isNotFound());

    ASSERT_TRUE(store.close().ok());
}
