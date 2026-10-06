#pragma once
// D1 — The OS boundary. Notes: Part 1 §6–§9 (page cache, syscalls, fsync, rename), Part 2 §10 (RAII).
//
// Everything that touches the kernel lives here, so the rest of the engine is testable and so you
// have ONE place to audit for syscall correctness (EINTR, partial writes, fsync errors, fd leaks).
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "lsm/status.hpp"

namespace dlsm {

using lsm::Status;

// Owning file descriptor (notes Part 2 §10). Move-only. Destructor closes.
class Fd {
public:
    Fd() = default;
    explicit Fd(int fd) noexcept;
    ~Fd();
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept;
    Fd& operator=(Fd&& other) noexcept;
    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }
    void reset() noexcept;

private:
    int fd_ = -1;
};

// Process-wide I/O counters for experiments ("how many preads did one Get cost?").
struct IoStats {
    std::atomic<std::uint64_t> preads{0}, pread_bytes{0};
    std::atomic<std::uint64_t> writes{0}, write_bytes{0};
    std::atomic<std::uint64_t> fsyncs{0};
};
IoStats& GlobalIoStats();

enum class OpenMode {
    kCreateNew,  // O_CREAT|O_EXCL — fail if the file exists (never clobber a table by accident)
    kTruncate,   // O_CREAT|O_TRUNC
    kAppend,     // O_CREAT|O_APPEND — continue an existing file
};

// Append-only file with a 64 KiB user-space buffer (like LevelDB's PosixWritableFile).
//  * Append() copies into the buffer; writes through when it fills.
//  * Flush() pushes the buffer to the kernel with a write() loop that handles partial writes and EINTR.
//  * Sync() = Flush() + fdatasync(). Count fsyncs in GlobalIoStats().
//  * The destructor flushes the user-space buffer to the kernel and closes, but never fsyncs —
//    callers decide durability.
class WritableFile {
public:
    static Status Open(const std::string& path, OpenMode mode, std::unique_ptr<WritableFile>* out);
    ~WritableFile();
    Status Append(std::string_view data);
    Status Flush();
    Status Sync();
    Status Close();               // Flush + close; idempotent
    std::uint64_t Size() const;   // bytes appended so far (including existing bytes in kAppend mode)
    const std::string& path() const { return path_; }

private:
    WritableFile() = default;
    Fd fd_;
    std::string path_;
    std::string buf_;
    std::uint64_t size_ = 0;
};

// Positional reads with pread() (notes Part 1 §7). Safe to call from many threads.
class RandomAccessFile {
public:
    static Status Open(const std::string& path, std::unique_ptr<RandomAccessFile>* out);
    // Read up to n bytes at offset into scratch; *result views the bytes read (shorter only at EOF).
    // Loops over short reads and EINTR. Counts preads in GlobalIoStats().
    Status Read(std::uint64_t offset, std::size_t n, char* scratch, std::string_view* result) const;
    std::uint64_t Size() const;
    const std::string& path() const { return path_; }

private:
    RandomAccessFile() = default;
    Fd fd_;
    std::string path_;
    std::uint64_t size_ = 0;
};

class SequentialFile {
public:
    static Status Open(const std::string& path, std::unique_ptr<SequentialFile>* out);
    // Read up to n bytes; *result is empty at EOF.
    Status Read(std::size_t n, char* scratch, std::string_view* result);
    Status Skip(std::uint64_t n);

private:
    SequentialFile() = default;
    Fd fd_;
};

// Holds flock(LOCK_EX|LOCK_NB) on a file for its lifetime (RAII).
class FileLock {
public:
    static Status Acquire(const std::string& path, std::unique_ptr<FileLock>* out);  // IOError if held
    ~FileLock();

private:
    FileLock() = default;
    Fd fd_;
};

Status CreateDirIfMissing(const std::string& dir);
Status SyncDir(const std::string& dir);                        // open(dir, O_RDONLY) + fsync
Status RenameFile(const std::string& from, const std::string& to);  // rename(2): atomic replace
Status RemoveFile(const std::string& path);
bool FileExists(const std::string& path);
Status GetFileSize(const std::string& path, std::uint64_t* size);
Status GetChildren(const std::string& dir, std::vector<std::string>* names);  // names only, no . / ..
Status WriteStringToFileSync(const std::string& path, std::string_view data);  // truncate+write+fsync
Status ReadFileToString(const std::string& path, std::string* data);

}  // namespace dlsm
