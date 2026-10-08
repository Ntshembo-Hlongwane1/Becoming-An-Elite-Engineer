#include "am/direct_file.hpp"

#include "am/todo.hpp"

namespace am {

DioAlignment QueryDioAlignment(int) { Todo("QueryDioAlignment"); }

// ---- DirectFile -------------------------------------------------------------------------------

DirectFile DirectFile::Open(const std::string&, int, IoMode, mode_t) { Todo("DirectFile::Open"); }

DirectFile::~DirectFile() {
    // TODO(Ex5): close if open. Never throw.
}

DirectFile::DirectFile(DirectFile&& other) noexcept {
    // TODO(Ex5): steal; leave other.fd_ == -1.
    (void)other;
}

DirectFile& DirectFile::operator=(DirectFile&& other) noexcept {
    // TODO(Ex5): close ours, steal theirs, survive self-move.
    (void)other;
    return *this;
}

std::size_t DirectFile::WriteAt(std::span<const char>, std::uint64_t) { Todo("DirectFile::WriteAt"); }
std::size_t DirectFile::ReadAt(std::span<char>, std::uint64_t) { Todo("DirectFile::ReadAt"); }
void DirectFile::Sync() { Todo("DirectFile::Sync"); }
void DirectFile::Truncate(std::uint64_t) { Todo("DirectFile::Truncate"); }
std::uint64_t DirectFile::Size() const { Todo("DirectFile::Size"); }
void DirectFile::Close() { Todo("DirectFile::Close"); }

// ---- DirectAppender ---------------------------------------------------------------------------

DirectAppender::DirectAppender(DirectFile& file, std::size_t block_size) : file_(file), block_size_(block_size) {
    Todo("DirectAppender::DirectAppender");
}

DirectAppender::~DirectAppender() {
    // TODO(Ex5): best-effort Flush(), swallow exceptions.
}

void DirectAppender::Append(std::span<const char>) { Todo("DirectAppender::Append"); }
void DirectAppender::Flush() { Todo("DirectAppender::Flush"); }
std::uint64_t DirectAppender::logical_size() const noexcept { return logical_size_; }
std::uint64_t DirectAppender::device_bytes_written() const noexcept { return device_bytes_; }

}  // namespace am
