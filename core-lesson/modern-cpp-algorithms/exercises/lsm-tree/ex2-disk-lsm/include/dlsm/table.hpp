#pragma once
// D3 — Sorted tables on disk (FORMAT.md §4; notes Part 6 §4–§8, §12).
// Reuses Exercise 1: lsm::BlockBuilder/BlockReader, lsm::BlockHandle/Footer, lsm::AppendBlock…,
// lsm::VerifyBlock, lsm::CreateBloomFilter. New here: the 64-byte HEADER and optional page ALIGNMENT,
// and reading through pread() instead of from a std::string.
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "dlsm/env.hpp"
#include "lsm/format.hpp"
#include "lsm/internal_key.hpp"
#include "lsm/iterator.hpp"

namespace dlsm {

struct TableOptions {
    std::size_t block_size = 4096 - lsm::kBlockTrailerSize;  // so block + trailer fits one page
    int restart_interval = 16;
    int bloom_bits_per_key = 10;
    std::uint32_t block_align = 0;  // 0 = packed; e.g. 4096 = pad before every data block and cut
                                    // blocks so block+trailer fits one page (FORMAT.md §4.4)
};

struct TableHeader {
    static constexpr std::size_t kSize = 64;
    static constexpr std::uint32_t kFlagAligned = 1;
    std::uint32_t format_version = 1;
    std::uint32_t flags = 0;
    std::uint32_t block_align = 0;
    std::uint32_t target_block_size = 0;
    std::uint64_t file_number = 0;
    void EncodeTo(std::string& dst) const;           // appends exactly 64 bytes
    Status DecodeFrom(std::string_view input);      // exactly 64 bytes; checks magic, version, crc
};

class TableBuilder {
public:
    // Writes the header immediately.
    TableBuilder(TableOptions options, WritableFile* file, std::uint64_t file_number);
    ~TableBuilder();
    void Add(std::string_view internal_key, std::string_view value);  // strictly increasing
    Status Finish();     // remaining data block, filter, metaindex, index, footer. Does NOT sync.
    void Abandon();      // stop; caller deletes the file
    Status status() const;
    std::uint64_t NumEntries() const;
    std::uint64_t FileSize() const;          // bytes written so far (== final size after Finish)
    std::string_view smallest() const;       // first internal key added
    std::string_view largest() const;        // last internal key added

private:
    TableOptions options_;
    WritableFile* file_;
    // your state
};

class Table : public std::enable_shared_from_this<Table> {
public:
    // Reads the footer (last 48 bytes) and header (first 64), then index, metaindex, filter (cached in
    // memory). Validates: sizes, magics, header crc, header.file_number == expected_number, every block
    // crc. Corruption => non-OK, never a crash.
    static Status Open(std::unique_ptr<RandomAccessFile> file, std::uint64_t expected_number,
                       std::shared_ptr<const Table>* out);

    // At most ONE data-block pread per call after Open (0 if the Bloom filter says no).
    Status Get(std::string_view user_key, lsm::SequenceNumber snapshot, lsm::LookupResult* result,
               std::string* value) const;
    std::unique_ptr<lsm::Iterator> NewIterator() const;  // keeps the Table alive

    const TableHeader& header() const;
    std::vector<lsm::BlockHandle> DataBlockHandles() const;  // from the index, in order (tests/tools)
    std::uint64_t file_size() const;
    std::uint64_t data_blocks_read() const;
    std::uint64_t bloom_negatives() const;

private:
    Table() = default;
    // your state
};

}  // namespace dlsm
