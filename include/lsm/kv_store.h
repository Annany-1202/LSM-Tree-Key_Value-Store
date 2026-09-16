#pragma once

#include "lsm/status.h"
#include "lsm/options.h"
#include <string>
#include <memory>

namespace lsm {

class KVStoreImpl;

class KVStore {
public:
    KVStore();
    explicit KVStore(const Options& options);
    ~KVStore();

    // Prevent copying
    KVStore(const KVStore&) = delete;
    KVStore& operator=(const KVStore&) = delete;

    // Allow moving
    KVStore(KVStore&&) noexcept;
    KVStore& operator=(KVStore&&) noexcept;

    Status open(const std::string& path);
    Status close();

    Status put(const std::string& key, const std::string& value);
    Result get(const std::string& key);
    Status remove(const std::string& key);

    // Optional helper to trigger a manual flush or compaction for testing/benchmarking
    Status flush();
    Status compact();

    // Inspector helpers for tests and benchmarks
    size_t sstableCount() const;
    size_t memtableSize() const;
    const Options& options() const;

private:
    std::unique_ptr<KVStoreImpl> impl_;
};

} // namespace lsm
