#include "dlsm/env.hpp"
#include "lsm/todo.hpp"

namespace dlsm {
using lsm::Todo;

Fd::Fd(int fd) noexcept : fd_(fd) {}
Fd::~Fd() { /* TODO(D1): close if valid */ }
Fd::Fd(Fd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
Fd& Fd::operator=(Fd&& other) noexcept { if (this != &other) { reset(); fd_ = other.fd_; other.fd_ = -1; } return *this; }
void Fd::reset() noexcept { /* TODO(D1): close if valid */ fd_ = -1; }

IoStats& GlobalIoStats() { static IoStats s; return s; }

Status WritableFile::Open(const std::string&, OpenMode, std::unique_ptr<WritableFile>*) { Todo("WritableFile::Open"); }
WritableFile::~WritableFile() = default;
Status WritableFile::Append(std::string_view) { Todo("WritableFile::Append"); }
Status WritableFile::Flush() { Todo("WritableFile::Flush"); }
Status WritableFile::Sync() { Todo("WritableFile::Sync"); }
Status WritableFile::Close() { Todo("WritableFile::Close"); }
std::uint64_t WritableFile::Size() const { Todo("WritableFile::Size"); }

Status RandomAccessFile::Open(const std::string&, std::unique_ptr<RandomAccessFile>*) { Todo("RandomAccessFile::Open"); }
Status RandomAccessFile::Read(std::uint64_t, std::size_t, char*, std::string_view*) const { Todo("RandomAccessFile::Read"); }
std::uint64_t RandomAccessFile::Size() const { Todo("RandomAccessFile::Size"); }

Status SequentialFile::Open(const std::string&, std::unique_ptr<SequentialFile>*) { Todo("SequentialFile::Open"); }
Status SequentialFile::Read(std::size_t, char*, std::string_view*) { Todo("SequentialFile::Read"); }
Status SequentialFile::Skip(std::uint64_t) { Todo("SequentialFile::Skip"); }

Status FileLock::Acquire(const std::string&, std::unique_ptr<FileLock>*) { Todo("FileLock::Acquire"); }
FileLock::~FileLock() = default;

Status CreateDirIfMissing(const std::string&) { Todo("CreateDirIfMissing"); }
Status SyncDir(const std::string&) { Todo("SyncDir"); }
Status RenameFile(const std::string&, const std::string&) { Todo("RenameFile"); }
Status RemoveFile(const std::string&) { Todo("RemoveFile"); }
bool FileExists(const std::string&) { Todo("FileExists"); }
Status GetFileSize(const std::string&, std::uint64_t*) { Todo("GetFileSize"); }
Status GetChildren(const std::string&, std::vector<std::string>*) { Todo("GetChildren"); }
Status WriteStringToFileSync(const std::string&, std::string_view) { Todo("WriteStringToFileSync"); }
Status ReadFileToString(const std::string&, std::string*) { Todo("ReadFileToString"); }

}  // namespace dlsm
