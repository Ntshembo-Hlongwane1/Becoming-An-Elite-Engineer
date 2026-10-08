#pragma once
// Exercise 5 (advanced) — O_DIRECT done properly. Notes: Part 5 (all of it), Part 6 B1–B5, Part 7 §4.
//
// Two classes:
//   DirectFile      — RAII fd + positional I/O that turns the kernel's vague EINVAL into a precise,
//                     named MisalignedIo error BEFORE the syscall, using the alignment that statx
//                     reports for THIS file (not a hard-coded 4096).
//   DirectAppender  — append arbitrary-sized data to a file through block-aligned O_DIRECT writes:
//                     the RocksDB tail-rewrite + ftruncate design (Part 5 §7).
//
// Uses Exercise 1 (alignment checks) and Exercise 3 (AlignedBuffer for the staging block).
#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

#include "am/aligned_buffer.hpp"

namespace am {

// ---------------------------------------------------------------------------------------------
// Alignment discovery (Part 5 §6)
// ---------------------------------------------------------------------------------------------
inline constexpr std::size_t kFallbackDioAlignment = 4096;  // PostgreSQL's choice (Part 5 §6)

struct DioAlignment {
    std::size_t memory = kFallbackDioAlignment;  // rule 1: buffer address
    std::size_t offset = kFallbackDioAlignment;  // rules 2+3: length and file offset
    bool reported = false;                       // true iff statx filled STATX_DIOALIGN with non-zero values
};

// statx(fd, "", AT_EMPTY_PATH, STATX_DIOALIGN, &sx). Check stx_mask! If the bit is missing or a value
// is 0, return the fallback with reported = false. If statx itself fails, throw std::system_error.
DioAlignment QueryDioAlignment(int fd);

// ---------------------------------------------------------------------------------------------
// The precise error (provided — it's boilerplate; read it, the message format is part of the lesson)
// ---------------------------------------------------------------------------------------------
class MisalignedIo : public std::invalid_argument {
public:
    enum class Rule { kAddress, kLength, kOffset };  // Part 5 §5: the three rules

    MisalignedIo(Rule rule, std::size_t required, std::uint64_t actual)
        : std::invalid_argument(Format(rule, required, actual)), rule_(rule), required_(required), actual_(actual) {}

    Rule rule() const noexcept { return rule_; }
    std::size_t required() const noexcept { return required_; }
    std::uint64_t actual() const noexcept { return actual_; }

private:
    static std::string Format(Rule r, std::size_t req, std::uint64_t act) {
        const char* what = r == Rule::kAddress ? "buffer address" : r == Rule::kLength ? "length" : "file offset";
        return std::string("O_DIRECT: ") + what + " " + std::to_string(act) + " is not a multiple of " +
               std::to_string(req) + " (see notes Part 5 §5)";
    }
    Rule rule_;
    std::size_t required_;
    std::uint64_t actual_;
};

// ---------------------------------------------------------------------------------------------
// DirectFile
// ---------------------------------------------------------------------------------------------
enum class IoMode { kBuffered, kDirect };

class DirectFile {
public:
    // open(path, flags | O_CLOEXEC | (mode == kDirect ? O_DIRECT : 0), perm).
    // Failure -> std::system_error(errno, std::generic_category(), "open " + path).
    // On success, query the alignment once and remember it.
    static DirectFile Open(const std::string& path, int flags, IoMode mode, mode_t perm = 0644);

    DirectFile() noexcept = default;
    ~DirectFile();                                   // closes if open; never throws
    DirectFile(const DirectFile&) = delete;          // Part 6 B1: the double-close bug
    DirectFile& operator=(const DirectFile&) = delete;
    DirectFile(DirectFile&& other) noexcept;         // moved-from: fd() == -1
    DirectFile& operator=(DirectFile&& other) noexcept;

    int fd() const noexcept { return fd_; }
    bool is_open() const noexcept { return fd_ >= 0; }
    IoMode mode() const noexcept { return mode_; }
    const DioAlignment& alignment() const noexcept { return align_; }
    const std::string& path() const noexcept { return path_; }

    // Write ALL of `data` at `offset` with pwrite (Part 5 §13), looping on partial writes and EINTR.
    // In kDirect mode, first validate the three rules in order address, length, offset and throw
    // MisalignedIo for the first one violated — before any syscall.
    // Decide (and write in DECISIONS.md) what you do if a direct pwrite returns a partial count that
    // is not a multiple of alignment().offset (Part 6 B3).
    // Syscall failure -> std::system_error(errno, std::generic_category(), "pwrite " + path()).
    // Returns data.size().
    std::size_t WriteAt(std::span<const char> data, std::uint64_t offset);

    // Read up to out.size() bytes at offset with pread, looping until `out` is full or EOF.
    // Same validation in kDirect mode. Returns the number of bytes read (short only at EOF — Part 5 §8).
    std::size_t ReadAt(std::span<char> out, std::uint64_t offset);

    void Sync();                          // fdatasync (Part 5 §9). Errors -> std::system_error.
    void Truncate(std::uint64_t size);    // ftruncate. Errors -> std::system_error.
    std::uint64_t Size() const;           // fstat st_size. Errors -> std::system_error.
    void Close();                         // idempotent. close() error -> std::system_error (fd is gone anyway).

private:
    int fd_ = -1;
    IoMode mode_ = IoMode::kBuffered;
    DioAlignment align_{};
    std::string path_;
};

// ---------------------------------------------------------------------------------------------
// DirectAppender — Part 5 §7, the RocksDB WriteDirect design
// ---------------------------------------------------------------------------------------------
// Invariants you must maintain:
//  * Every WriteAt is exactly one full block at a block-aligned offset (so it is legal under O_DIRECT).
//  * The staging block is an AlignedBuffer aligned to file.alignment().memory.
//  * After Flush(): the file's size == logical_size() exactly, and the bytes on disk past the logical
//    end inside the last block are ZERO (Part 7 §4 — never stale heap data).
//  * Appending after Flush() continues seamlessly (the tail block is rewritten in place).
class DirectAppender {
public:
    // `file` must outlive the appender and must be empty (Size() == 0), else std::invalid_argument.
    // block_size must be > 0 and a multiple of file.alignment().offset AND of file.alignment().memory,
    // else std::invalid_argument.
    DirectAppender(DirectFile& file, std::size_t block_size);
    ~DirectAppender();  // best-effort Flush(); must not throw (catch and swallow — say why in DECISIONS.md)

    DirectAppender(const DirectAppender&) = delete;
    DirectAppender& operator=(const DirectAppender&) = delete;

    void Append(std::span<const char> data);   // any size, including 0 and >> block_size
    void Flush();                              // complete blocks + zero-padded tail, then Truncate(logical_size())

    std::uint64_t logical_size() const noexcept;         // total bytes appended
    std::uint64_t device_bytes_written() const noexcept;  // sum of all WriteAt sizes, rewrites included (Ex 6)

    // Provided, read-only: the full staging block exactly as the next WriteAt would send it to the
    // device. Tests use it to prove the padding is zero — after ftruncate the file API can no
    // longer show those bytes, but they DID reach the disk (data remanence, Part 7 §4).
    // If you rename block_, keep this accessor working.
    std::span<const char> staging_block() const noexcept { return block_.padded_span(); }

private:
    // Suggested members (you may change them):
    DirectFile& file_;
    std::size_t block_size_;
    AlignedBuffer block_;              // staging block, zero-filled
    std::size_t used_ = 0;             // bytes of block_ holding real data
    std::uint64_t block_offset_ = 0;   // file offset where block_ will be written
    std::uint64_t logical_size_ = 0;
    std::uint64_t device_bytes_ = 0;
};

}  // namespace am
