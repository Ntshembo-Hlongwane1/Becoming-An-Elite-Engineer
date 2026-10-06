#include "dlsm/log.hpp"
#include "lsm/todo.hpp"

namespace dlsm::log {
using lsm::Todo;

Writer::Writer(WritableFile* dest, std::uint64_t dest_length) : dest_(dest), block_offset_(dest_length % kBlockSize) {}
Status Writer::AddRecord(std::string_view) { Todo("log::Writer::AddRecord"); }

Reader::Reader(SequentialFile* file, Reporter* reporter, bool verify_checksums)
    : file_(file), reporter_(reporter), verify_(verify_checksums) {}
bool Reader::ReadRecord(std::string*) { Todo("log::Reader::ReadRecord"); }

}  // namespace dlsm::log
