#pragma once

#include "lsm/status.h"
#include "lsm/options.h"
#include "lsm/sstable.h"
#include <string>
#include <vector>

namespace lsm {

class Compaction {
public:
    // Merges multiple SSTables into a single new SSTable.
    // sstable_paths is ordered from oldest to newest: sstable_paths[0] is oldest, sstable_paths.back() is newest.
    // Writes to a temporary file first, validates it, renames to out_sstable_path,
    // and then safely deletes old SSTables if remove_old is true.
    static Status compact(
        const std::vector<std::string>& input_sstable_paths,
        const std::string& out_sstable_path,
        const Options& options = Options{},
        bool remove_old = true
    );
};

} // namespace lsm
