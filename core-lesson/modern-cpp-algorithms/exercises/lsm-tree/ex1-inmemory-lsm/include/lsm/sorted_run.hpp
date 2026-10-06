#pragma once
// M7 — An in-memory SSTable ("sorted run"). Notes: Part 6 §4–§8.
//
// Bytes produced by SortedRunBuilder::Finish (all blocks followed by the 5-byte trailer):
//   data blocks (BlockBuilder, internal keys, cut when CurrentSizeEstimate() >= block_size)
//   filter block   = CreateBloomFilter over the USER keys of the whole run
//   metaindex block= one entry:  "filter.minilsm.bloom" -> BlockHandle(filter)
//   index block    = restart_interval 1; key = shortest separator (user-key part shortened, then
//                    re-wrapped as an internal key — see DECISIONS hint below), value = BlockHandle
//   footer         = 48 bytes (format.hpp)
//
// DECISIONS hint: LevelDB shortens the USER-key part of the separator and, if it changed, appends
// fixed64(kMaxSequenceNumber<<8 | kValueTypeForSeek) so the separator is still an internal key that
// sorts before every real entry with that user key. Work out why that tag is the right one.
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "lsm/block.hpp"
#include "lsm/format.hpp"
#include "lsm/internal_key.hpp"
#include "lsm/iterator.hpp"
#include "lsm/status.hpp"

namespace lsm {

struct RunOptions {
    std::size_t block_size = 4096;
    int restart_interval = 16;
    int bloom_bits_per_key = 10;   // 0 = no filter block
};

class SortedRunBuilder {
public:
    explicit SortedRunBuilder(RunOptions options = {});
    // Internal keys in strictly increasing InternalKeyCompare order.
    void Add(std::string_view internal_key, std::string_view value);
    // Writes the complete run (blocks, filter, metaindex, index, footer) into *out.
    Status Finish(std::string* out);
    std::uint64_t NumEntries() const;

private:
    RunOptions options_;
    // your state here
};

// Derives from enable_shared_from_this so NewIterator() can hand the iterator a shared_ptr that keeps
// the run (and its bytes) alive — the lifetime rule of notes Part 2 §11 / Part 5 §10.
class SortedRun : public std::enable_shared_from_this<SortedRun> {
public:
    // Parse `bytes` exactly like a disk reader: footer (last 48 bytes) -> index block -> metaindex
    // -> filter. Every block's CRC is verified. Garbage => Status::Corruption, never a crash.
    static Status Open(std::shared_ptr<const std::string> bytes, std::uint64_t number,
                       std::shared_ptr<const SortedRun>* out);

    // Newest version of user_key with seq <= snapshot. Must consult the Bloom filter first and
    // read (verify + parse) at most ONE data block.
    Status Get(std::string_view user_key, SequenceNumber snapshot, LookupResult* result,
               std::string* value) const;

    // Iterates all (internal_key, value) in order. Keeps this run alive while iterating.
    std::unique_ptr<Iterator> NewIterator() const;

    std::string_view smallest() const;   // smallest internal key
    std::string_view largest() const;    // largest internal key
    std::uint64_t number() const;        // like a file number: bigger = newer
    std::size_t size_bytes() const;

    // Statistics for M10 (mutable, relaxed atomics).
    std::uint64_t bloom_negatives() const;
    std::uint64_t data_blocks_read() const;

private:
    SortedRun() = default;
    // your state here
};

}  // namespace lsm
