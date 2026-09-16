cmake_minimum_required(VERSION 3.20)

project(LSMTreeKVStore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add compiler warnings and optimization for release
if(MSVC)
    add_compile_options(/W4 /O2)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O2)
endif()

# Core LSM library
set(LSM_SOURCES
    src/memtable.cpp
    src/wal.cpp
    src/bloom_filter.cpp
    src/sstable.cpp
    src/compaction.cpp
    src/kv_store.cpp
)

add_library(lsm_core STATIC ${LSM_SOURCES})
target_include_directories(lsm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Test executable
set(TEST_SOURCES
    tests/test_main.cpp
    tests/basic_test.cpp
    tests/memtable_test.cpp
    tests/wal_test.cpp
    tests/bloom_filter_test.cpp
    tests/sstable_test.cpp
    tests/compaction_test.cpp
    tests/integration_test.cpp
)

add_executable(lsm_tests ${TEST_SOURCES})
target_link_libraries(lsm_tests PRIVATE lsm_core)
target_include_directories(lsm_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)

# Benchmark executable
add_executable(lsm_benchmark benchmarks/benchmark.cpp)
target_link_libraries(lsm_benchmark PRIVATE lsm_core)
target_include_directories(lsm_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Interactive Demonstration CLI
add_executable(lsm_cli src/cli.cpp)
target_link_libraries(lsm_cli PRIVATE lsm_core)
target_include_directories(lsm_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

enable_testing()
add_test(NAME AllTests COMMAND lsm_tests)
