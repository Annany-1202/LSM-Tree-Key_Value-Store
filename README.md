# Persistent LSM-Tree Key-Value Store

A persistent Log-Structured Merge-tree (LSM-Tree) key-value storage engine implemented from scratch in C++20.

> **Project Status**: This is an educational systems project built from first principles to explore LSM-tree internals, binary storage formats, indexing, probabilistic filtering, and crash recovery. It is not intended as a production replacement for mature storage engines such as RocksDB or LevelDB.

---

## Why I Built This

Modern databases like RocksDB, LevelDB, and Cassandra rely on Log-Structured Merge-trees for write-intensive workloads. The purpose of this project was to understand storage systems below the database abstraction by implementing an LSM-Tree storage engine from first principles.

This project implements the end-to-end data lifecycle:
- Durability logging via a sequential Write-Ahead Log (WAL)
- In-memory sorted buffering via a MemTable
- Immutable on-disk persistence via custom binary SSTables
- Point lookup optimization using sparse indexing and Bloom filters
- Deletion semantics via tombstones
- Space reclamation and version consolidation via compaction
- Crash recovery and replay mechanics

---

## Key Features

- **Core Operations**: Persistent `PUT`, `GET`, and `DELETE` key-value API.
- **MemTable**: In-memory sorted buffer tracking approximate memory usage with configurable byte flush thresholds.
- **Write-Ahead Log (WAL)**: Synchronous append-only binary log with per-record CRC32 checksums for restart recovery.
- **Custom SSTable Binary Format**: Structured into header, sorted data records, sparse index, Bloom filter, and fixed-size footer.
- **Sparse Index**: In-memory index sampling keys at fixed record intervals (default: 16) to bound sequential disk scans.
- **Bloom Filter**: In-memory probabilistic filter with double hashing (FNV-1a and Jenkins) to avoid disk reads for absent keys.
- **Tombstones**: Soft-deletion markers shadowing obsolete keys across older SSTables.
- **Compaction**: Synchronous threshold-triggered multi-way merge consolidating SSTables, dropping superseded versions, and purging tombstones.
- **Crash Recovery**: Replays committed WAL records on restart to reconstruct in-memory state; detects truncated tail records.
- **Defensive Error Handling**: Explicit status codes (`OK`, `NOT_FOUND`, `IO_ERROR`, `CORRUPTION`, `INVALID_ARGUMENT`) with CRC32 integrity checks.
- **CLI & Test Suite**: Interactive command-line client and 25 automated unit and integration tests.

---

## Architecture

```text
                     ┌──────────────────────┐
                     │       KVStore        │
                     │   PUT / GET / DELETE │
                     └──────────┬───────────┘
                                │
                 ┌──────────────┴──────────────┐
                 │                             │
                 ▼                             ▼
               WAL                         MemTable
          (wal.log)                            │
                 │                     (size threshold)
                 │                             │
                 │                             ▼
                 │                          SSTable
                 │                 (Header -> Data -> Index ->
                 │                  Bloom Filter -> Footer)
                 ▼
           WAL Recovery


 Multiple SSTables (newest -> oldest)
                 │
                 ▼
       Synchronous Compaction
                 │
                 ▼
          Compacted SSTable
```

### Write Path
1. The MemTable checks if approximate memory usage exceeds `memtable_size_threshold`. If exceeded, it triggers an immutable SSTable flush to disk.
2. The mutation is appended to the binary WAL (`wal.log`) with length prefixes and a CRC32 checksum, followed by a stream flush.
3. The mutation is applied to the in-memory MemTable (`std::map`). Deletions are stored as explicit tombstone records.

### Read Path
1. **MemTable**: Evaluated first. If present as a value, returns `OK`. If present as a tombstone, returns `NotFound`.
2. **SSTables (Newest to Oldest)**:
   - **Bloom Filter**: Evaluated in RAM. If definitely absent, disk reads for that SSTable are avoided entirely.
   - **Sparse Index**: Binary searched in RAM to find the candidate byte offset range `[start_offset, end_offset)`.
   - **Data Scan**: Reads sequential records in the candidate range. If found as a value, returns `OK`. If found as a tombstone, stops traversal and returns `NotFound`.
3. If no SSTable contains the key, returns `NotFound`.

### Recovery Path
When opening an existing database:
1. `manifest.txt` is loaded to discover and open all active SSTables in chronological order.
2. The WAL file (`wal.log`) is scanned sequentially. Valid records are re-applied into the MemTable.
3. If an ungraceful shutdown or process crash leaves an incomplete trailing record, length checks and CRC32 verification detect the truncation, allowing the engine to recover all valid records committed prior to the truncation point without corrupting existing data.

### Compaction Path
When the number of active SSTables reaches `compaction_threshold` (default: 4), a synchronous compaction run triggers:
1. All active SSTables are read in chronological order.
2. Records are merged in sorted order. Newer records overwrite older records with the same key.
3. Obsolete duplicate versions are dropped, and tombstones are safely discarded.
4. The output is staged to a temporary SSTable (`.compact_tmp`), validated by `SSTableReader`, atomically renamed into place, and recorded in `manifest.txt`.
5. Older input SSTables are deleted only after the new table is verified and registered.

---

## Quick Start

### Prerequisites
- **C++ Compiler**: GCC/G++ with C++20 support (tested with GCC 16.2.0 on MSYS2 UCRT64)
- **Build System**: CMake $\ge$ 3.20 and Ninja

### Build
From the repository root:

```powershell
cmake -S . -B build -G Ninja
cmake --build build
```

This compiles:
- `build/liblsm_core.a` (Core storage engine library)
- `build/lsm_cli.exe` (Interactive CLI demo)
- `build/lsm_tests.exe` (Automated test suite)
- `build/lsm_benchmark.exe` (Performance benchmark suite)

### Run Tests
```powershell
.\build\lsm_tests.exe
```

Run specific test suites:
```powershell
.\build\lsm_tests.exe MemTable
.\build\lsm_tests.exe WAL
.\build\lsm_tests.exe SSTable
.\build\lsm_tests.exe Compaction
.\build\lsm_tests.exe Integration
```

### Run Benchmarks
```powershell
.\build\lsm_benchmark.exe
```

---

## Usage

### Interactive CLI
Launch the interactive shell against a target directory:

```powershell
.\build\lsm_cli.exe .\demo_db
```

Example session:
```text
annany_lsm> PUT user123 Annany Sharma
OK [WAL appended]

annany_lsm> GET user123
Annany Sharma

annany_lsm> FLUSH
FLUSH complete
SSTable created: sst_000001.sst
Active SSTables: 1

annany_lsm> DELETE user123
OK [TOMBSTONE written to WAL/MemTable]

annany_lsm> GET user123
NOT_FOUND

annany_lsm> PUT user789 Bob Dylan
OK [WAL appended]

annany_lsm> EXIT
Database closed. Goodbye!
```

Reopening the database against the same directory recovers unflushed data from the WAL:
```text
annany_lsm> GET user789
Bob Dylan
```

### C++ API Example
```cpp
#include "lsm/kv_store.h"
#include <iostream>

int main() {
    lsm::Options options;
    options.memtable_size_threshold = 4 * 1024 * 1024; // 4 MB
    options.compaction_threshold = 4;

    lsm::KVStore store(options);
    lsm::Status s = store.open("./my_database");
    if (!s.ok()) {
        std::cerr << "Open error: " << s.toString() << "\n";
        return 1;
    }

    // Write key-value pairs
    store.put("session:1001", "active");
    store.put("session:1002", "idle");

    // Point lookup
    lsm::Result r = store.get("session:1001");
    if (r.ok()) {
        std::cout << "Value: " << r.value << "\n";
    }

    // Delete (writes tombstone)
    store.remove("session:1002");

    // Close cleanly
    store.close();
    return 0;
}
```

---

## Benchmarks

### Benchmark Environment
- **OS**: Windows 11
- **Compiler**: GCC 16.2.0 (MSYS2 UCRT64), compiled with `-O2`
- **Build System**: Ninja 1.13.2

### Baseline Performance
Workload: 50,000 sequential `PUT` operations followed by 50,000 `GET` operations on existing keys (17-byte formatted keys `user_key_%08zu`, 100-byte values, 2 MB MemTable threshold).

| Operation | Throughput (ops/sec) | Avg Latency ($\mu$s) | p50 ($\mu$s) | p95 ($\mu$s) | p99 ($\mu$s) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **PUT (Write)** | **156,812.6** | **6.27** | **2.10** | **3.20** | **8.50** |
| **GET (Read)**  | **59,174.0**  | **16.78** | **16.40** | **19.10** | **26.50** |

### Storage Overhead
- **Logical Key/Value Data**: 5.58 MB (5,850,000 bytes across 50,000 entries)
- **Actual Directory Footprint**: 6.35 MB (6,653,386 bytes)
- **Storage Overhead Ratio**: **1.137x** (measured for this specific benchmark workload; accounts for headers, footers, sparse index entries, Bloom filter bit arrays, and per-record CRC32 checksums)

### MemTable Size Experiment
Workload: 40,000 write and read operations (10-byte keys `k_%08zu`, 120-byte payloads) evaluated across three MemTable size thresholds.

| MemTable Size | Write Thpt (ops/s) | Read Thpt (ops/s) | SSTables Produced | Actual Size (MB) | Overhead Ratio |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **1 MB** | 129,298.0 | 59,241.9 | 2 | 5.56 | 1.120x |
| **4 MB** | 270,408.4 | 60,436.2 | 2 | 5.56 | 1.120x |
| **16 MB** | 309,759.6 | 60,228.5 | 1 | 5.56 | 1.120x |

*Finding*: In this benchmark workload, increasing the MemTable threshold from 1 MB to 16 MB increased measured write throughput from ~129K to ~310K ops/s (~2.4x) by amortizing I/O flushes and compaction passes.

### Bloom Filter Experiment (Missing-Key Reads)
Workload: 20,000 populated keys; 20,000 random missing-key lookups evaluated with the Bloom filter disabled vs enabled across an SSTable.

| Configuration | Missing-Key Read Thpt (ops/s) | Avg Latency ($\mu$s) | p50 ($\mu$s) | p99 ($\mu$s) |
| :--- | :--- | :--- | :--- | :--- |
| **Bloom Filter OFF** | 57,833.3 | 17.17 | 16.50 | 29.60 |
| **Bloom Filter ON** | **2,590,069.7** | **0.31** | **0.20** | **0.30** |

*Finding*: In this benchmark workload, enabling the Bloom filter increased missing-key lookup throughput from ~57.8K to ~2.59M ops/s (~44.8x), avoiding SSTable data and index reads for keys that the Bloom filter determines are definitely absent.

---

## Engineering Trade-Offs

- **Synchronous WAL Flushing vs Group Commit**: V1 uses synchronous writes and flushes for each WAL mutation. While group committing (batching multiple concurrent writes) would increase throughput under concurrent workloads, synchronous flushing was chosen for single-threaded predictability and simpler crash-recovery reasoning.
- **`std::map` MemTable vs SkipList**: The MemTable uses `std::map` (Red-Black tree) from the C++ standard library. It provides logarithmic search and insertion complexity along with strictly sorted iteration required for SSTable flushing. A custom SkipList was avoided to maintain simplicity, clear ownership semantics, and standard library reliance within the single-threaded V1 scope.
- **Sparse Index Sampling Interval (16 records)**: Sampling every 16th record balances in-memory index size against disk search time. Once candidate bounds are identified, scanning up to 16 sequential records from disk is fast due to sequential I/O and operating system read-ahead.
- **Single-Level Basic Compaction vs Multi-Tier Leveled Compaction**: V1 implements basic compaction that merges all active SSTables into a single new SSTable. This eliminates the complexity of leveled file placement while guaranteeing that tombstones can be purged safely without resurrecting shadowed data from older levels.

---

## Testing

The project includes an automated test runner (`lsm_tests`) covering **25 tests** (all currently passing):
- **MemTable**: Insertion, update, tombstone handling, size accounting, sorted iteration, and threshold triggers.
- **WAL**: Record encoding, replay recovery, detection of mid-write file truncation, and payload corruption detection via CRC32.
- **Bloom Filter**: Zero false negatives across 1,000+ keys, bounded false-positive rates, and binary serialization roundtrips.
- **SSTable & Sparse Index**: File generation, binary layout verification, range search correctness, and invalid header/footer rejection.
- **Compaction**: Multi-table merging, duplicate resolution (newer-value-wins), tombstone purging, and safe file replacement via staging files.
- **Integration**: Full lifecycle persistence across process restarts, unflushed WAL replay, and automatic threshold compaction.

---

## Project Structure

```text
.
├── CMakeLists.txt              # Root build configuration
├── LICENSE                     # MIT License
├── README.md                   # Project documentation
├── include/
│   └── lsm/
│       ├── kv_store.h          # Public KVStore API
│       ├── memtable.h          # In-memory sorted buffer
│       ├── wal.h               # Write-Ahead Log writer and recovery
│       ├── sstable.h           # SSTable writer, reader, index, and footer
│       ├── bloom_filter.h      # Probabilistic Bloom filter
│       ├── compaction.h        # SSTable merge and compaction routines
│       ├── status.h            # Status and Result types
│       ├── types.h             # Record definitions and CRC32 utility
│       └── options.h           # Engine configuration parameters
├── src/
│   ├── kv_store.cpp            # Database coordinator
│   ├── memtable.cpp            # MemTable implementation
│   ├── wal.cpp                 # Binary WAL implementation
│   ├── sstable.cpp             # Binary SSTable implementation
│   ├── bloom_filter.cpp        # Bloom filter implementation
│   ├── compaction.cpp          # Compaction routines
│   └── cli.cpp                 # Interactive CLI driver
├── tests/
│   ├── test_framework.h        # Minimal testing framework
│   ├── test_main.cpp           # Test executable runner
│   ├── memtable_test.cpp       # MemTable unit tests
│   ├── wal_test.cpp            # WAL and recovery unit tests
│   ├── bloom_filter_test.cpp   # Bloom filter unit tests
│   ├── sstable_test.cpp        # SSTable and index unit tests
│   ├── compaction_test.cpp     # Compaction unit tests
│   └── integration_test.cpp    # End-to-end integration tests
└── benchmarks/
    └── benchmark.cpp           # Throughput, latency, and experiment suites
```

---

## Limitations & Future Work

### Current Limitations
- **Single-Threaded**: Single process, single thread; no concurrent read/write support or background threads.
- **Embedded Operation**: Local filesystem access only; no client/server network layer, SQL, or REST interfaces.
- **No In-Memory Block Cache**: Data block reads rely directly on operating system page caching rather than a dedicated application block cache.
- **Single-Level Compaction**: All tables are consolidated in a single pass rather than partitioned into leveled hierarchies (e.g., Level 0 to Level $N$).

### Future Work (V2 Directions)
- **Concurrent MemTable**: Replace `std::map` with a concurrent SkipList to allow concurrent writes and background flush threads.
- **Leveled Compaction**: Implement a LevelDB-style leveled compaction strategy with non-overlapping key ranges per level.
- **Block Cache**: Add an LRU uncompressed block cache to reduce read latency on hot data.
- **Iterators / Range Queries**: Expose forward and reverse iterators over the unified LSM view.

---

## Documentation

- System specification: [Project_Specs/LSM-Tree_KV_Store.md](Project_Specs/LSM-Tree_KV_Store.md)
