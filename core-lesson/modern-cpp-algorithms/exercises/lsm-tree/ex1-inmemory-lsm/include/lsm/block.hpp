#pragma once
// M5 — Data/index blocks with prefix compression and restart points. Notes: Part 6 §5.
//
// Entry   := varint32 shared | varint32 non_shared | varint32 value_len | key_delta | value
// Trailer := fixed32 restarts[num_restarts] | fixed32 num_restarts
// restarts[0] == 0; a restart entry has shared == 0. Keys must be strictly increasing under cmp.
//
// The block TRAILER (type byte + CRC) is NOT part of these bytes — see format.hpp.
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "lsm/internal_key.hpp"
#include "lsm/iterator.hpp"
#include "lsm/status.hpp"

namespace lsm {

struct BlockOptions {
    int restart_interval = 16;
    CompareFn cmp = BytewiseCompare;
};

class BlockBuilder {
public:
    explicit BlockBuilder(BlockOptions options = {});
    void Add(std::string_view key, std::string_view value);
    std::string_view Finish();               // appends the trailer; valid until Reset()
    void Reset();
    std::size_t CurrentSizeEstimate() const;  // bytes Finish() would produce
    bool empty() const;

private:
    BlockOptions options_;
    std::string buffer_;
    std::vector<std::uint32_t> restarts_;
    int counter_ = 0;
    bool finished_ = false;
    std::string last_key_;
};

class BlockReader {
public:
    // Validates the trailer (enough bytes, num_restarts consistent with size, every restart offset
    // inside the entry area). On garbage: Status::Corruption, never a crash. `contents` must outlive
    // the reader and all its iterators (they return views into it).
    static Status Open(std::string_view contents, CompareFn cmp, std::unique_ptr<BlockReader>* out);

    std::uint32_t NumRestarts() const;
    // Iterator::Seek must binary-search the restart array, then scan linearly.
    // Corrupt entries discovered while iterating make the iterator invalid with a non-OK status().
    std::unique_ptr<Iterator> NewIterator() const;

private:
    BlockReader() = default;
    std::string_view data_;
    std::uint32_t restart_offset_ = 0;   // where the restart array begins
    std::uint32_t num_restarts_ = 0;
    CompareFn cmp_ = BytewiseCompare;
};

}  // namespace lsm
