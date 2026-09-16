# Persistent LSM-Tree Key-Value Store — V1 Build Specification

## 1. Mission

Build a small, serious, persistent **LSM-Tree key-value storage engine from scratch in C++**.

This is **V1 only**.

The objective is to produce a complete, working, understandable storage engine demonstrating:

- systems programming
- data structures
- persistent storage
- file I/O
- binary file formats
- crash recovery
- indexing
- probabilistic data structures
- compaction
- performance measurement
- clean C++ architecture

This is an educational systems project, not a production database.

Existing systems such as RocksDB and LevelDB may be studied for architectural understanding, but **their code or exact file formats must not be copied**.

---

# 2. Absolute Scope Rule

**Build exactly the V1 described in this document.**

Do not:

- implement features outside this specification
- implement future versions
- design elaborate abstractions for hypothetical future features
- add networking
- add concurrency
- add distributed functionality
- add a GUI
- add SQL
- add caching
- add features merely because they make the project sound more impressive

Do not ask whether something should be implemented in V2.

**There is no V2 in this task.**

If a feature is not explicitly required by this specification, do not implement it.

When every V1 acceptance criterion passes, **STOP**.

---

# 3. Technology

Use:

- Language: C++
- Standard: C++17 or C++20
- Build system: CMake using Ninja (I have also attached MY SYSTEM SPECIFIC CMake configration files, use that to build the cmake project)
- Platform: Windows 11
- Compiler: g++ (Rev3, Built by MSYS2 project) 16.2.0
- Storage: local filesystem
- Testing: GoogleTest or another lightweight C++ testing framework
- Benchmarking: simple custom benchmark executable

Do not use an external database.

The storage engine must operate as a local embedded library/application.

Avoid unnecessary third-party dependencies.

---

# 4. V1 Features

V1 consists of exactly these components:

1. `KVStore`
2. `MemTable`
3. Write-Ahead Log
4. WAL recovery
5. SSTable writer
6. SSTable reader
7. Custom SSTable binary format
8. Sparse index
9. Bloom filter
10. Tombstones
11. Basic compaction
12. Restart persistence
13. Basic corruption/error handling
14. Configurable MemTable size
15. Automated tests
16. Benchmarks
17. README/documentation

These components together constitute the complete V1.

---

# 5. V1 Architecture

Use this architecture:

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
                │                             │
                │                       size threshold
                │                             │
                │                             ▼
                │                         SSTable
                │                             │
                │                 ┌───────────┼───────────┐
                │                 │           │           │
                │                 ▼           ▼           ▼
                │               Data       Index       Bloom
                │
                ▼
          WAL Recovery


Multiple SSTables
       │
       ▼
   Basic Compaction
       │
       ▼
Compacted SSTable
```

The engine is:

- single process
- single machine
- local filesystem
- single-threaded
- synchronous

No concurrency is required.

---

# 6. Public API

Implement a simple C++ API equivalent to:

```cpp
class KVStore {
public:
    Status open(const std::string& path);
    Status close();

    Status put(
        const std::string& key,
        const std::string& value
    );

    Result get(const std::string& key);

    Status remove(const std::string& key);
};
```

The exact `Status` and `Result` implementation is up to the implementation, but the three operations must exist:

```text
PUT
GET
DELETE
```

The API does not require:

- SQL
- query language
- HTTP
- TCP
- REST
- Redis protocol
- authentication
- authorization
- transactions
- replication

---

# 7. MemTable

## 7.1 Purpose

The MemTable stores the latest in-memory state before data is flushed to disk.

## 7.2 Requirements

The MemTable must:

- maintain keys in sorted order
- support insertion
- support update
- support lookup
- support deletion
- support sorted iteration
- store tombstones
- track approximate memory usage
- have a configurable size threshold

Use:

```cpp
std::map
```

or an equivalent standard sorted container.

Do not implement a custom tree or skip list.

## 7.3 Representation

Each entry must represent either:

```text
PUT:
key → value

DELETE:
key → TOMBSTONE
```

A DELETE must remain represented in the MemTable until it is persisted/compacted.

## 7.4 Flush

When the MemTable reaches its configured size threshold:

```text
MemTable
   │
   ▼
freeze
   │
   ▼
write SSTable
   │
   ▼
new empty MemTable
```

The threshold must be configurable in bytes.

Only one active MemTable is required.

---

# 8. Write-Ahead Log

## 8.1 Purpose

Every mutation must first be written to the WAL.

Required ordering:

```text
PUT / DELETE
      │
      ▼
 append WAL record
      │
      ▼
 update MemTable
```

A mutation is not considered durable until its WAL record has been written according to the implementation's durability policy.

For V1, use a simple synchronous approach.

Do not implement advanced durability mechanisms.

## 8.2 WAL Format

Use a custom binary record format.

Use a structure equivalent to:

```text
┌────────────┬────────────┬──────────────┬────────────────┐
│ RecordType │ Key Length │ Value Length │ Payload        │
│  1 byte    │ 4 bytes    │ 4 bytes      │ key + value   │
└────────────┴────────────┴──────────────┴────────────────┘
```

Where:

```text
RecordType = PUT or DELETE
```

For DELETE, the value length may be zero.

A checksum may be included if useful, but it is not required.

The chosen WAL format must be documented in the README.

## 8.3 Required WAL Operations

Implement:

- append
- flush
- replay
- PUT records
- DELETE records
- malformed-record detection
- truncated-record detection

---

# 9. WAL Recovery

When opening an existing database:

```text
open database
      │
      ▼
locate WAL
      │
      ▼
read WAL
      │
      ▼
validate records
      │
      ▼
replay records
      │
      ▼
reconstruct MemTable
      │
      ▼
database ready
```

Example:

```text
PUT A 100
PUT B 200
DELETE C
crash
```

After restart:

```text
GET A → 100
GET B → 200
GET C → NOT FOUND
```

If the final WAL record is incomplete or malformed, the implementation must detect it.

Do not interpret arbitrary incomplete bytes as a valid record.

---

# 10. SSTables

## 10.1 Purpose

When the MemTable reaches its configured size, write its contents to an immutable SSTable.

SSTable records must be sorted by key.

Example:

```text
apple
banana
cat
dog
elephant
```

## 10.2 Required Properties

Every SSTable must be:

- immutable
- sorted
- persistent
- readable after restart
- stored using a custom binary format

---

# 11. SSTable Format

Design a simple custom binary SSTable format.

Use this logical layout:

```text
┌──────────────────────────┐
│ Header                   │
├──────────────────────────┤
│ Data Records             │
├──────────────────────────┤
│ Sparse Index             │
├──────────────────────────┤
│ Bloom Filter             │
├──────────────────────────┤
│ Footer / Metadata        │
└──────────────────────────┘
```

The implementation must document:

- magic number
- format version
- record count
- data location
- index location
- Bloom filter location
- metadata
- key encoding
- value encoding
- record encoding
- offsets

Checksums are optional.

The format must be deterministic and understandable.

Do not copy the LevelDB or RocksDB SSTable format.

---

# 12. SSTable Records

Each data record must contain enough information to represent:

```text
PUT:
key
value

DELETE:
key
tombstone
```

Keys must appear in sorted order.

The SSTable must provide enough metadata for the reader to locate and decode records safely.

---

# 13. GET Lookup

Implement GET in this order:

```text
GET(key)
   │
   ▼
MemTable
   │
   ├── found PUT → return value
   │
   ├── found TOMBSTONE → NOT_FOUND
   │
   └── not found
          │
          ▼
      SSTables
          │
          ▼
      newest → oldest
          │
          ▼
      Bloom Filter
          │
          ├── definitely absent → skip
          │
          └── possibly present
                  │
                  ▼
                Index
                  │
                  ▼
              disk lookup
```

Newer data must always override older data.

For multiple SSTables, search newest SSTable first.

---

# 14. Sparse Index

Every SSTable must contain a basic sparse index.

The index maps representative keys to file offsets.

Example:

```text
apple     → 0
cat       → 1200
elephant  → 2400
orange    → 3600
```

Use a simple strategy such as indexing every Nth record.

The index must allow the reader to identify a reasonable location from which to search for the requested key.

The exact indexing interval may be configurable.

Do not implement:

- B-tree
- B+ tree
- learned index
- multi-level index
- adaptive index

A simple sparse index is sufficient.

---

# 15. Bloom Filter

Each SSTable must contain a Bloom filter.

Its purpose is to determine whether a key is definitely absent before performing disk lookup.

Lookup behavior:

```text
Bloom Filter
     │
     ├── definitely absent
     │        │
     │        ▼
     │      skip SSTable
     │
     └── possibly present
              │
              ▼
            Index
              │
              ▼
          disk lookup
```

Implement the Bloom filter using:

- bit array
- multiple hash functions

The configuration must allow the false-positive behavior to be controlled through parameters such as:

- bits per key
- number of hash functions

Required properties:

- no false negatives
- false positives are acceptable

Do not implement other probabilistic structures.

---

# 16. Tombstones

DELETE must create a tombstone.

Example:

```text
Older SSTable:
user123 → Annany

Newer SSTable:
user123 → TOMBSTONE
```

GET must return:

```text
NOT_FOUND
```

The tombstone must override older values.

During compaction, obsolete values hidden by tombstones may be removed.

---

# 17. Basic Compaction

Implement one simple compaction mechanism.

The purpose is to merge multiple SSTables into fewer SSTables.

Use a simple trigger such as:

```text
if number of SSTables >= N:
    compact
```

The value of `N` may be configurable.

## 17.1 Compaction Behavior

When compacting:

- read sorted entries
- merge entries
- resolve duplicate keys
- newer entries override older entries
- tombstones override older values
- obsolete duplicate values are removed
- produce a new valid SSTable

Example:

```text
Older:
A → 10

Newer:
A → 20

Result:
A → 20
```

Example:

```text
Older:
A → 10

Newer:
A → TOMBSTONE

Result:
A → removed
```

## 17.2 Safe Replacement

The new SSTable must be completely written and valid before old SSTables are deleted.

The implementation must never delete the only valid copy of data before the replacement SSTable is ready.

Compaction is synchronous.

---

# 18. Persistence

Restart persistence is mandatory.

This sequence must work:

```text
create database
      │
      ▼
PUT keys
      │
      ▼
DELETE keys
      │
      ▼
flush / close / terminate
      │
      ▼
reopen database
      │
      ▼
GET keys
      │
      ▼
previous state preserved
```

On restart, the engine must reconstruct state from:

1. existing SSTables
2. WAL records that have not yet been flushed into SSTables

---

# 19. Error Handling

Implement basic defensive error handling.

The engine must detect and return explicit errors for conditions such as:

- invalid arguments
- inability to create/open the database directory
- inability to open a file
- malformed WAL record
- truncated WAL record
- invalid SSTable magic number
- unsupported SSTable version
- invalid SSTable metadata
- invalid offsets
- corrupted records
- invalid file structure

Use a simple error representation such as:

```cpp
enum class StatusCode {
    OK,
    NOT_FOUND,
    IO_ERROR,
    CORRUPTION,
    INVALID_ARGUMENT
};
```

The exact implementation may differ.

Do not build an elaborate error framework.

---

# 20. Testing

Automated tests are mandatory.

## 20.1 MemTable Tests

Test:

- insert
- update
- lookup
- delete
- tombstone
- sorted iteration
- size tracking
- flush threshold

## 20.2 WAL Tests

Test:

- writing a PUT
- writing a DELETE
- reading records
- replay
- PUT recovery
- DELETE recovery
- malformed record
- truncated record

## 20.3 SSTable Tests

Test:

- creation
- sorted output
- reading
- missing key
- tombstone record
- persistence
- invalid header
- invalid metadata

## 20.4 Index Tests

Test:

- index creation
- correct key/offset mapping
- lookup behavior

## 20.5 Bloom Filter Tests

Test:

- inserted keys are always reported as possible
- missing keys can produce false positives
- no false negatives

## 20.6 Tombstone Tests

Test:

- deleting an existing key
- deleting a nonexistent key
- tombstone overriding an older SSTable value

## 20.7 Compaction Tests

Test:

- merging SSTables
- duplicate key resolution
- newer value wins
- tombstone handling
- old SSTable replacement
- resulting SSTable correctness

## 20.8 End-to-End Tests

At minimum:

```text
PUT
GET
DELETE
flush
restart
GET
```

Also test:

```text
PUT
flush
PUT updated value
flush
GET
```

and:

```text
PUT
flush
DELETE
flush
GET
```

The expected result must always be correct.

Tests must run through CMake/CTest or the selected testing framework.

---

# 21. Benchmarks

Implement a simple reproducible benchmark executable.

Measure:

## Write Throughput

```text
PUT operations
operations / second
```

## Read Throughput

```text
GET operations
operations / second
```

## Latency

Report:

- average
- p50
- p95
- p99

## Storage Overhead

Measure:

```text
logical key/value data size
vs
actual database directory size
```

Do not fabricate benchmark results.

Run the benchmarks and put the actual measured results in the README.

---

# 22. Required Benchmark Comparisons

Perform these two experiments.

## Experiment 1 — MemTable Size

Run at least three configurations:

```text
1 MB
4 MB
16 MB
```

Measure:

- write throughput
- read performance
- number of SSTables
- storage overhead
- compaction frequency

Use the actual results from the implementation.

## Experiment 2 — Bloom Filter

Compare:

```text
Bloom filter OFF
Bloom filter ON
```

Measure missing-key read performance.

Again, use actual measured results.

---

# 23. Project Structure

Use a clean modular structure similar to:

```text
lsm-kv-store/
│
├── CMakeLists.txt
├── README.md
├── LICENSE
│
├── include/
│   └── lsm/
│       ├── kv_store.h
│       ├── memtable.h
│       ├── wal.h
│       ├── sstable.h
│       ├── index.h
│       ├── bloom_filter.h
│       ├── compaction.h
│       ├── status.h
│       └── ...
│
├── src/
│   ├── kv_store.cpp
│   ├── memtable.cpp
│   ├── wal.cpp
│   ├── sstable.cpp
│   ├── index.cpp
│   ├── bloom_filter.cpp
│   ├── compaction.cpp
│   └── ...
│
├── tests/
│   ├── memtable_test.cpp
│   ├── wal_test.cpp
│   ├── sstable_test.cpp
│   ├── bloom_filter_test.cpp
│   ├── compaction_test.cpp
│   └── integration_test.cpp
│
├── benchmarks/
│   └── benchmark.cpp
│
└── data/
```

The exact file organization may differ if necessary, but responsibilities must remain modular.

Do not create a monolithic source file.

---

# 24. Required Implementation Order

Implement V1 incrementally in this exact order.

## Step 1 — Project Foundation

Build:

```text
CMake
KVStore skeleton
Status / Result
tests
```

Verify the project builds and tests run.

---

## Step 2 — MemTable

Implement:

```text
MemTable
PUT
GET
DELETE
tombstones
sorted iteration
size tracking
```

Verify with tests.

---

## Step 3 — WAL

Implement:

```text
WAL writer
WAL reader
PUT records
DELETE records
record validation
```

Integrate:

```text
PUT → WAL → MemTable
DELETE → WAL → MemTable
```

Verify with tests.

---

## Step 4 — WAL Recovery

Implement:

```text
database open
WAL replay
MemTable reconstruction
restart recovery
```

Verify that data survives process termination.

---

## Step 5 — SSTable

Implement:

```text
MemTable flush
SSTable writer
custom binary format
SSTable reader
```

Verify:

```text
MemTable
   ↓
SSTable
   ↓
reader
   ↓
GET
```

---

## Step 6 — Sparse Index

Add:

```text
SSTable index
index lookup
indexed disk search
```

Verify with tests.

---

## Step 7 — Bloom Filter

Add:

```text
Bloom filter creation
Bloom filter serialization
Bloom filter loading
Bloom filter lookup
```

Integrate it into SSTable GET.

Verify no false negatives.

---

## Step 8 — Multiple SSTables

Support:

```text
SSTable 1
SSTable 2
SSTable 3
...
```

Search newest to oldest.

Verify that newer values override older values.

---

## Step 9 — Compaction

Implement:

```text
compaction trigger
SSTable merge
duplicate resolution
tombstone handling
safe replacement
```

Verify with automated tests.

---

## Step 10 — Reliability

Add:

```text
error handling
corruption detection
truncated WAL detection
invalid SSTable detection
restart tests
end-to-end tests
```

---

## Step 11 — Benchmarks

Implement:

```text
write benchmark
read benchmark
latency measurement
storage measurement
MemTable experiments
Bloom filter experiment
```

Run them and record the real results.

---

## Step 12 — Documentation

Write the README containing:

- project overview
- architecture
- build instructions
- usage example
- API
- WAL format
- SSTable format
- index design
- Bloom filter design
- compaction behavior
- persistence/recovery behavior
- testing instructions
- benchmark methodology
- actual benchmark results
- engineering trade-offs

---

# 25. V1 Definition of Done

The project is COMPLETE only when all of these work.

## API

```text
PUT
GET
DELETE
open
close
```

## Memory

```text
MemTable
sorted keys
tombstones
configurable size
```

## Durability

```text
WAL
WAL replay
restart recovery
```

## Disk Storage

```text
SSTables
custom binary format
sorted records
SSTable reader
```

## Lookup

```text
Bloom filter
sparse index
newest-to-oldest search
```

## Deletion

```text
tombstones
```

## Maintenance

```text
basic compaction
duplicate resolution
safe SSTable replacement
```

## Reliability

```text
basic corruption detection
malformed WAL detection
truncated WAL detection
restart persistence
```

## Quality

```text
unit tests
integration tests
benchmarks
README
```

All tests must pass.

The benchmark executable must run successfully.

The README must contain actual benchmark results.

---

# 26. Explicitly Forbidden

Do not implement any of the following:

- SQL
- query language
- TCP server
- HTTP server
- REST API
- Redis protocol
- authentication
- authorization
- transactions
- MVCC
- replication
- sharding
- distributed storage
- consensus
- Raft
- clustering
- multiple SSTable levels
- leveled compaction
- tiered compaction
- sophisticated compaction policies
- concurrent reads
- concurrent writes
- background threads
- thread pools
- asynchronous I/O
- lock-free structures
- custom memory allocators
- custom balanced trees
- custom skip lists
- B-trees
- B+ trees
- LRU cache
- block cache
- compression
- encryption
- snapshots
- secondary indexes
- range queries
- columnar storage
- query optimizer
- schema system
- plugin architecture
- GUI
- web UI
- cloud deployment
- Kubernetes
- microservices
- Docker unless required strictly for development reproducibility

If something is not required by this document, **do not add it**.

---

# 27. Engineering Rules

Prioritize, in order:

1. Correctness
2. Simplicity
3. Clear ownership
4. Testability
5. Understandable persistence formats
6. Measurable performance
7. Explicit engineering trade-offs

Follow these rules:

- Do not optimize prematurely.
- Do not introduce abstractions without a concrete V1 need.
- Prefer the C++ standard library where appropriate.
- Keep components small and understandable.
- Keep persistence formats explicit.
- Validate data read from disk.
- Never silently ignore corruption.
- Never fabricate benchmark results.
- Do not copy database implementations.
- Do not over-engineer the system.

The goal is not to recreate RocksDB.

The goal is to build a **small, correct, understandable LSM-tree storage engine from scratch**.

---

# 28. Agent Execution Rules

You are the implementation agent.

Work directly toward completing this V1 specification.

### Rule 1 — Do not expand scope

If you think of a feature that is not explicitly required, do not implement it.

### Rule 2 — Do not prematurely generalize

Do not build interfaces or abstractions solely because a hypothetical larger system might need them.

Implement what V1 requires.

### Rule 3 — Make reasonable decisions

Where this specification gives multiple acceptable implementation choices, choose the simplest reasonable option and proceed.

Do not stop to ask unnecessary design questions.

### Rule 4 — Preserve correctness

If an implementation decision affects correctness, persistence, crash recovery, SSTable integrity, or data visibility, prioritize correctness over simplicity.

### Rule 5 — Test continuously

After implementing each component:

1. build
2. run tests
3. fix failures
4. integrate with existing components
5. continue to the next component

Do not implement the entire system blindly and test only at the end.

### Rule 6 — Inspect your own work

Before declaring V1 complete:

- build from a clean state
- run the complete test suite
- run integration tests
- test restart persistence
- test corruption handling
- run benchmarks
- verify generated SSTables
- verify WAL recovery
- verify compaction
- verify DELETE semantics
- verify Bloom filter has no false negatives
- verify the README is accurate

### Rule 7 — Stop at V1

Once all requirements in the **V1 Definition of Done** are satisfied and the tests pass:

**STOP IMPLEMENTING.**

Do not continue by adding extra features.

---

# 29. Final Expected Result

The final project should feel like:

> A small, understandable, persistent LSM-tree key-value storage engine implemented from scratch in C++.

It should have:

```text
PUT
  ↓
WAL
  ↓
MemTable
  ↓
SSTable
  ↓
Compaction
```

Reads should use:

```text
MemTable
   ↓
SSTables
   ↓
Bloom Filter
   ↓
Sparse Index
   ↓
Disk
```

Deletes should use:

```text
DELETE
   ↓
Tombstone
   ↓
SSTable
   ↓
Compaction
```

Recovery should use:

```text
WAL
 ↓
Replay
 ↓
MemTable
```

The final implementation must be:

- correct
- persistent
- tested
- benchmarked
- documented
- modular
- understandable

**This specification defines the complete scope.**

**Implement V1. Nothing more. Nothing less.**