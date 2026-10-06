#pragma once
// M8–M10 — The in-memory LSM-tree. Notes: Part 5 (all of it).
//
// Single-threaded API (concurrency lives inside the skip list, M3). Structure:
//   mem_  (MemTable)  --flush-->  L0: runs ordered NEWEST FIRST (may overlap)
//   L1..Lmax: each level is ONE sorted run made of non-overlapping SortedRuns, ordered by key.
// Triggers:
//   * memtable ApproximateMemoryUsage() >= memtable_bytes          -> flush to L0
//   * L0 run count >= l0_compaction_trigger                        -> merge ALL L0 + overlapping L1
//   * bytes(Ln) > level1_bytes * level_ratio^(n-1)   (n >= 1)      -> pick one run of Ln (round-robin
//     by key, Part 5 §7) + overlapping runs of Ln+1, merge into Ln+1
//   Output runs are cut at target_run_bytes.
// Drop rules during compaction: LevelDB's (A) and (B) — notes Part 5 §7 — with snapshots.
// Output cutting: only cut a new output run at a USER-key boundary. If two versions of one user key
// land in two adjacent runs of the same level, the runs overlap in user-key space and a point lookup
// that checks "the one run whose range contains the key" can miss a version. (Tests check that
// runs in L>=1 have strictly increasing, non-overlapping user-key ranges.)
// Lifetime: an iterator may outlive a flush. Keep the memtable alive (e.g. hold it in a shared_ptr
// that the iterator also owns).
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "lsm/iterator.hpp"
#include "lsm/memtable.hpp"
#include "lsm/sorted_run.hpp"
#include "lsm/status.hpp"

namespace lsm {

struct Options {
    std::size_t memtable_bytes = 64 * 1024;
    int l0_compaction_trigger = 4;
    int level_ratio = 10;                     // T
    std::size_t level1_bytes = 256 * 1024;
    std::size_t target_run_bytes = 64 * 1024;
    int num_levels = 7;
    bool use_heap_merge = false;
    RunOptions run;
};

class WriteBatch {  // provided
public:
    void Put(std::string_view key, std::string_view value) {
        ops_.push_back({ValueType::kValue, std::string(key), std::string(value)});
    }
    void Delete(std::string_view key) { ops_.push_back({ValueType::kDeletion, std::string(key), {}}); }
    struct Op {
        ValueType type;
        std::string key;
        std::string value;
    };
    const std::vector<Op>& ops() const { return ops_; }
    void Clear() { ops_.clear(); }

private:
    std::vector<Op> ops_;
};

struct Snapshot {
    SequenceNumber sequence;
};

struct ReadOptions {
    std::shared_ptr<const Snapshot> snapshot;  // null = latest
};

struct Stats {
    std::uint64_t user_bytes_written = 0;        // key+value bytes passed to Put/Delete/Write
    std::uint64_t flush_bytes_written = 0;       // bytes of runs produced by flushes
    std::uint64_t compaction_bytes_read = 0;
    std::uint64_t compaction_bytes_written = 0;
    std::uint64_t gets = 0;
    std::uint64_t runs_probed = 0;               // SortedRun::Get calls made by DB Get
    std::vector<std::size_t> runs_per_level;
    std::vector<std::size_t> bytes_per_level;
    double WriteAmplification() const {
        return user_bytes_written == 0
                   ? 0.0
                   : static_cast<double>(flush_bytes_written + compaction_bytes_written) /
                         static_cast<double>(user_bytes_written);
    }
};

class MemDB {
public:
    explicit MemDB(Options options = {});
    ~MemDB();
    MemDB(const MemDB&) = delete;
    MemDB& operator=(const MemDB&) = delete;

    void Put(std::string_view key, std::string_view value);
    void Delete(std::string_view key);
    void Write(const WriteBatch& batch);  // atomic: consecutive sequence numbers, applied together

    // NotFound if absent or deleted.
    Status Get(const ReadOptions& options, std::string_view key, std::string* value);
    // User keys in order, live values only, at the snapshot (or latest).
    std::unique_ptr<Iterator> NewIterator(const ReadOptions& options);

    // Snapshot lifetime = lifetime of the returned shared_ptr. The DB must find the smallest live
    // snapshot when compacting (hint: keep std::weak_ptr<const Snapshot>s).
    std::shared_ptr<const Snapshot> GetSnapshot();

    void FlushMemTable();       // force: memtable -> L0 (no-op if empty), then run triggered compactions
    void CompactAll();          // merge everything into the last non-empty level (a "major compaction")

    SequenceNumber LastSequence() const;
    Stats GetStats() const;
    std::string DebugString() const;  // e.g. "L0: [3 runs] L1: [a..f][g..z] …" — for you
    // For tests: per level, the USER-key [smallest, largest] of each run, in the level's order
    // (L0 newest first; L>=1 ascending by key).
    std::vector<std::vector<std::pair<std::string, std::string>>> DebugLevelUserRanges() const;

private:
    Options options_;
    // your state: mem_, levels_ (vector<vector<shared_ptr<const SortedRun>>>), sequence counter,
    // next run number, snapshots list, compaction pointers, stats …
};

}  // namespace lsm
