#pragma once
// D5 — The on-disk LSM engine. Notes: Part 5 (engine) + Part 6 (formats, durability order, recovery).
// Reuse your Exercise 1 MemDB logic (compaction picking, drop rules, iterators) — the differences
// are: runs are files, the WAL comes first, and every structural change is a MANIFEST edit.
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "dlsm/env.hpp"
#include "dlsm/table.hpp"
#include "lsm/iterator.hpp"
#include "lsm/memdb.hpp"   // lsm::WriteBatch, lsm::ReadOptions, lsm::Snapshot, lsm::Stats

namespace dlsm {

struct DiskOptions {
    bool create_if_missing = true;
    bool error_if_exists = false;
    std::size_t memtable_bytes = 4 * 1024 * 1024;
    int l0_compaction_trigger = 4;
    int level_ratio = 10;
    std::size_t level1_bytes = 10 * 1024 * 1024;
    std::size_t target_file_bytes = 2 * 1024 * 1024;
    int num_levels = 7;
    TableOptions table;
};

struct WriteOptions {
    bool sync = false;  // true: fdatasync the WAL before returning (notes Part 5 §3)
};

class DiskDB {
public:
    // Recovery per FORMAT.md §7. Holds LOCK until destroyed.
    static Status Open(const DiskOptions& options, const std::string& dir, std::unique_ptr<DiskDB>* out);
    // Closes files and releases LOCK. Does NOT flush the memtable — the WAL already has the data.
    ~DiskDB();

    Status Put(const WriteOptions& options, std::string_view key, std::string_view value);
    Status Delete(const WriteOptions& options, std::string_view key);
    Status Write(const WriteOptions& options, const lsm::WriteBatch& batch);

    Status Get(const lsm::ReadOptions& options, std::string_view key, std::string* value);
    std::unique_ptr<lsm::Iterator> NewIterator(const lsm::ReadOptions& options);
    std::shared_ptr<const lsm::Snapshot> GetSnapshot();

    Status FlushMemTable();   // memtable -> L0 table with the full durability protocol (FORMAT.md §6)
    Status CompactAll();

    lsm::Stats GetStats() const;
    std::vector<std::vector<std::pair<std::string, std::string>>> DebugLevelUserRanges() const;
    std::vector<std::string> DebugLiveFiles() const;  // bare names of live tables, e.g. "000012.sst"

private:
    DiskDB() = default;
    // your state
};

}  // namespace dlsm
