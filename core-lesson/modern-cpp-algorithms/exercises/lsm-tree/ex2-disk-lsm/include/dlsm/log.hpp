#pragma once
// D2 — Log format writer/reader (FORMAT.md §2; notes Part 6 §3). Used for the WAL and the MANIFEST.
#include <cstdint>
#include <string>
#include <string_view>

#include "dlsm/env.hpp"

namespace dlsm::log {

inline constexpr std::size_t kBlockSize = 32768;
inline constexpr std::size_t kHeaderSize = 4 + 2 + 1;
enum RecordType : std::uint8_t { kZeroType = 0, kFullType = 1, kFirstType = 2, kMiddleType = 3, kLastType = 4 };

class Writer {
public:
    // dest_length = current size of dest (non-zero when continuing an existing log).
    explicit Writer(WritableFile* dest, std::uint64_t dest_length = 0);
    // Fragments `record` into FULL/FIRST/MIDDLE/LAST physical records. Calls dest->Flush() after each
    // physical record (so a process crash loses nothing that AddRecord returned OK for). Does NOT sync.
    Status AddRecord(std::string_view record);

private:
    WritableFile* dest_;
    std::size_t block_offset_;
};

class Reader {
public:
    struct Reporter {
        virtual ~Reporter() = default;
        // `bytes` were dropped because of `reason`. Never called for a torn tail at EOF.
        virtual void Corruption(std::size_t bytes, const Status& reason) = 0;
    };
    Reader(SequentialFile* file, Reporter* reporter, bool verify_checksums);
    // Next complete user record into *record. False at EOF (including a torn tail).
    bool ReadRecord(std::string* record);

private:
    SequentialFile* file_;
    Reporter* reporter_;
    bool verify_;
    // your state: a 32 KiB backing buffer, the unread part of the current block, eof flag, ...
};

}  // namespace dlsm::log
