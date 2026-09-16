#include "lsm/kv_store.h"
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <iomanip>
#include <filesystem>
#include <random>

namespace fs = std::filesystem;

struct BenchmarkStats {
    double total_time_sec = 0.0;
    double throughput_ops_sec = 0.0;
    double avg_latency_us = 0.0;
    double p50_latency_us = 0.0;
    double p95_latency_us = 0.0;
    double p99_latency_us = 0.0;
};

static BenchmarkStats calculateStats(std::vector<double>& latencies_us, double total_time_sec, size_t ops) {
    BenchmarkStats stats;
    stats.total_time_sec = total_time_sec;
    stats.throughput_ops_sec = (total_time_sec > 0) ? (static_cast<double>(ops) / total_time_sec) : 0;

    if (latencies_us.empty()) return stats;

    std::sort(latencies_us.begin(), latencies_us.end());
    double sum = std::accumulate(latencies_us.begin(), latencies_us.end(), 0.0);
    stats.avg_latency_us = sum / latencies_us.size();

    size_t p50_idx = static_cast<size_t>(latencies_us.size() * 0.50);
    size_t p95_idx = static_cast<size_t>(latencies_us.size() * 0.95);
    size_t p99_idx = static_cast<size_t>(latencies_us.size() * 0.99);

    if (p50_idx >= latencies_us.size()) p50_idx = latencies_us.size() - 1;
    if (p95_idx >= latencies_us.size()) p95_idx = latencies_us.size() - 1;
    if (p99_idx >= latencies_us.size()) p99_idx = latencies_us.size() - 1;

    stats.p50_latency_us = latencies_us[p50_idx];
    stats.p95_latency_us = latencies_us[p95_idx];
    stats.p99_latency_us = latencies_us[p99_idx];

    return stats;
}

static uint64_t getDirectorySize(const std::string& path) {
    uint64_t total = 0;
    std::error_code ec;
    if (!fs::exists(path, ec)) return 0;
    for (const auto& entry : fs::recursive_directory_iterator(path, ec)) {
        if (fs::is_regular_file(entry.status(ec))) {
            total += fs::file_size(entry.path(), ec);
        }
    }
    return total;
}

static void printStatsHeader() {
    std::cout << std::left
              << std::setw(15) << "Throughput(ops)"
              << std::setw(15) << "Avg Lat(us)"
              << std::setw(15) << "p50(us)"
              << std::setw(15) << "p95(us)"
              << std::setw(15) << "p99(us)"
              << std::endl;
    std::cout << std::string(75, '-') << std::endl;
}

static void printStatsRow(const BenchmarkStats& stats) {
    std::cout << std::left
              << std::setw(15) << std::fixed << std::setprecision(1) << stats.throughput_ops_sec
              << std::setw(15) << std::fixed << std::setprecision(2) << stats.avg_latency_us
              << std::setw(15) << std::fixed << std::setprecision(2) << stats.p50_latency_us
              << std::setw(15) << std::fixed << std::setprecision(2) << stats.p95_latency_us
              << std::setw(15) << std::fixed << std::setprecision(2) << stats.p99_latency_us
              << std::endl;
}

// ---------------------------------------------------------------------------
// Standard Baseline Benchmark
// ---------------------------------------------------------------------------
void runBaselineBenchmark() {
    std::cout << "\n======================================================\n";
    std::cout << " [1] STANDARD BASELINE BENCHMARK (50,000 Operations)\n";
    std::cout << "======================================================\n";

    const std::string db_dir = "benchmark_baseline_db";
    fs::remove_all(db_dir);

    const size_t kNumOps = 50000;
    const size_t kValSize = 100;
    std::string sample_val(kValSize, 'v');

    lsm::Options options;
    options.memtable_size_threshold = 2 * 1024 * 1024; // 2MB
    options.compaction_threshold = 4;

    lsm::KVStore store(options);
    store.open(db_dir);

    uint64_t logical_bytes = 0;

    // 1. Write Benchmark
    std::cout << "Running PUT Benchmark (" << kNumOps << " records)...\n";
    std::vector<double> put_latencies;
    put_latencies.reserve(kNumOps);

    auto put_start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < kNumOps; ++i) {
        char key_buf[32];
        std::snprintf(key_buf, sizeof(key_buf), "user_key_%08zu", i);
        std::string key = key_buf;
        logical_bytes += key.size() + sample_val.size();

        auto t0 = std::chrono::high_resolution_clock::now();
        store.put(key, sample_val);
        auto t1 = std::chrono::high_resolution_clock::now();

        double lat = std::chrono::duration<double, std::micro>(t1 - t0).count();
        put_latencies.push_back(lat);
    }
    auto put_end = std::chrono::high_resolution_clock::now();
    double put_total_sec = std::chrono::duration<double>(put_end - put_start).count();

    BenchmarkStats put_stats = calculateStats(put_latencies, put_total_sec, kNumOps);
    std::cout << "\nWRITE (PUT) PERFORMANCE:\n";
    printStatsHeader();
    printStatsRow(put_stats);

    // Flush to ensure all data is in SSTables for read testing
    store.flush();

    // 2. Read Benchmark (Existing Keys)
    std::cout << "\nRunning GET Benchmark (Existing Keys, " << kNumOps << " lookups)...\n";
    std::vector<double> get_latencies;
    get_latencies.reserve(kNumOps);

    auto get_start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < kNumOps; ++i) {
        char key_buf[32];
        std::snprintf(key_buf, sizeof(key_buf), "user_key_%08zu", i);

        auto t0 = std::chrono::high_resolution_clock::now();
        lsm::Result r = store.get(key_buf);
        auto t1 = std::chrono::high_resolution_clock::now();

        double lat = std::chrono::duration<double, std::micro>(t1 - t0).count();
        get_latencies.push_back(lat);

        if (!r.ok()) {
            std::cerr << "Warning: Key not found: " << key_buf << std::endl;
        }
    }
    auto get_end = std::chrono::high_resolution_clock::now();
    double get_total_sec = std::chrono::duration<double>(get_end - get_start).count();

    BenchmarkStats get_stats = calculateStats(get_latencies, get_total_sec, kNumOps);
    std::cout << "\nREAD (GET) PERFORMANCE:\n";
    printStatsHeader();
    printStatsRow(get_stats);

    // 3. Storage Overhead
    uint64_t actual_bytes = getDirectorySize(db_dir);
    double overhead_ratio = static_cast<double>(actual_bytes) / static_cast<double>(logical_bytes);

    std::cout << "\nSTORAGE OVERHEAD MEASUREMENT:\n";
    std::cout << " - Logical Data Size:     " << (logical_bytes / 1024.0 / 1024.0) << " MB (" << logical_bytes << " bytes)\n";
    std::cout << " - Actual Directory Size: " << (actual_bytes / 1024.0 / 1024.0) << " MB (" << actual_bytes << " bytes)\n";
    std::cout << " - Storage Overhead Ratio:" << std::fixed << std::setprecision(3) << overhead_ratio << "x\n";
    std::cout << " - Active SSTable Files:  " << store.sstableCount() << "\n";

    store.close();
    fs::remove_all(db_dir);
}

// ---------------------------------------------------------------------------
// Experiment 1 — MemTable Size: 1 MB vs 4 MB vs 16 MB
// ---------------------------------------------------------------------------
void runExperiment1_MemTableSize() {
    std::cout << "\n======================================================\n";
    std::cout << " [2] EXPERIMENT 1: MemTable Size (1MB vs 4MB vs 16MB)\n";
    std::cout << "======================================================\n";

    const size_t kNumOps = 40000;
    const size_t kValSize = 120;
    std::string sample_val(kValSize, 'e');

    std::vector<std::pair<std::string, size_t>> configs = {
        {"1 MB", 1 * 1024 * 1024},
        {"4 MB", 4 * 1024 * 1024},
        {"16 MB", 16 * 1024 * 1024}
    };

    std::cout << std::left
              << std::setw(12) << "MemTable"
              << std::setw(18) << "Write Thpt (ops/s)"
              << std::setw(18) << "Read Thpt (ops/s)"
              << std::setw(14) << "SSTables"
              << std::setw(18) << "Actual Size (MB)"
              << std::setw(16) << "Overhead Ratio"
              << std::endl;
    std::cout << std::string(96, '-') << std::endl;

    for (const auto& [label, threshold] : configs) {
        std::string db_dir = "exp1_db_" + label;
        fs::remove_all(db_dir);

        lsm::Options options;
        options.memtable_size_threshold = threshold;
        options.compaction_threshold = 4;

        lsm::KVStore store(options);
        store.open(db_dir);

        uint64_t logical_bytes = 0;

        // Write workload
        auto put_start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < kNumOps; ++i) {
            char key_buf[32];
            std::snprintf(key_buf, sizeof(key_buf), "k_%08zu", i);
            logical_bytes += std::string(key_buf).size() + sample_val.size();
            store.put(key_buf, sample_val);
        }
        auto put_end = std::chrono::high_resolution_clock::now();
        double write_sec = std::chrono::duration<double>(put_end - put_start).count();
        double write_thpt = kNumOps / write_sec;

        store.flush();

        // Read workload
        auto get_start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < kNumOps; ++i) {
            char key_buf[32];
            std::snprintf(key_buf, sizeof(key_buf), "k_%08zu", i);
            store.get(key_buf);
        }
        auto get_end = std::chrono::high_resolution_clock::now();
        double read_sec = std::chrono::duration<double>(get_end - get_start).count();
        double read_thpt = kNumOps / read_sec;

        uint64_t actual_bytes = getDirectorySize(db_dir);
        double actual_mb = actual_bytes / 1024.0 / 1024.0;
        double ratio = static_cast<double>(actual_bytes) / static_cast<double>(logical_bytes);
        size_t sst_count = store.sstableCount();

        std::cout << std::left
                  << std::setw(12) << label
                  << std::setw(18) << std::fixed << std::setprecision(1) << write_thpt
                  << std::setw(18) << std::fixed << std::setprecision(1) << read_thpt
                  << std::setw(14) << sst_count
                  << std::setw(18) << std::fixed << std::setprecision(2) << actual_mb
                  << std::setw(16) << std::fixed << std::setprecision(3) << ratio
                  << std::endl;

        store.close();
        fs::remove_all(db_dir);
    }
}

// ---------------------------------------------------------------------------
// Experiment 2 — Bloom Filter: OFF vs ON (Missing-Key Read Performance)
// ---------------------------------------------------------------------------
void runExperiment2_BloomFilter() {
    std::cout << "\n======================================================\n";
    std::cout << " [3] EXPERIMENT 2: Bloom Filter (OFF vs ON)\n";
    std::cout << "     Missing-Key Read Throughput & Latency\n";
    std::cout << "======================================================\n";

    const size_t kPopulateKeys = 20000;
    const size_t kQueryKeys = 20000;
    const std::string val = "bloom_benchmark_test_payload_12345";

    for (bool enable_bloom : {false, true}) {
        std::string mode_str = enable_bloom ? "Bloom Filter ON " : "Bloom Filter OFF";
        std::string db_dir = enable_bloom ? "exp2_bloom_on" : "exp2_bloom_off";
        fs::remove_all(db_dir);

        lsm::Options options;
        options.enable_bloom_filter = enable_bloom;
        options.memtable_size_threshold = 256 * 1024; // 256 KB to produce several SSTables
        options.compaction_threshold = 10; // Keep multiple SSTables to demonstrate Bloom advantage

        lsm::KVStore store(options);
        store.open(db_dir);

        // Populate database
        for (size_t i = 0; i < kPopulateKeys; ++i) {
            char key_buf[32];
            std::snprintf(key_buf, sizeof(key_buf), "existing_key_%08zu", i);
            store.put(key_buf, val);
        }
        store.flush();

        // Query missing keys
        std::vector<double> missing_latencies;
        missing_latencies.reserve(kQueryKeys);

        auto start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < kQueryKeys; ++i) {
            char key_buf[32];
            std::snprintf(key_buf, sizeof(key_buf), "non_existent_key_%08zu", i);

            auto t0 = std::chrono::high_resolution_clock::now();
            lsm::Result r = store.get(key_buf);
            auto t1 = std::chrono::high_resolution_clock::now();

            double lat = std::chrono::duration<double, std::micro>(t1 - t0).count();
            missing_latencies.push_back(lat);
        }
        auto end = std::chrono::high_resolution_clock::now();
        double total_sec = std::chrono::duration<double>(end - start).count();

        BenchmarkStats stats = calculateStats(missing_latencies, total_sec, kQueryKeys);

        std::cout << "\nConfiguration: [" << mode_str << "] across " << store.sstableCount() << " SSTables:\n";
        printStatsHeader();
        printStatsRow(stats);

        store.close();
        fs::remove_all(db_dir);
    }
}

int main() {
    std::cout << "Starting Persistent LSM-Tree Storage Engine Benchmarks...\n";
    runBaselineBenchmark();
    runExperiment1_MemTableSize();
    runExperiment2_BloomFilter();
    std::cout << "\nBenchmarks completed successfully!\n";
    return 0;
}
