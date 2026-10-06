#include "dlsm/db.hpp"
#include "lsm/todo.hpp"

namespace dlsm {
using lsm::Todo;

Status DiskDB::Open(const DiskOptions&, const std::string&, std::unique_ptr<DiskDB>*) { Todo("DiskDB::Open"); }
DiskDB::~DiskDB() = default;
Status DiskDB::Put(const WriteOptions&, std::string_view, std::string_view) { Todo("DiskDB::Put"); }
Status DiskDB::Delete(const WriteOptions&, std::string_view) { Todo("DiskDB::Delete"); }
Status DiskDB::Write(const WriteOptions&, const lsm::WriteBatch&) { Todo("DiskDB::Write"); }
Status DiskDB::Get(const lsm::ReadOptions&, std::string_view, std::string*) { Todo("DiskDB::Get"); }
std::unique_ptr<lsm::Iterator> DiskDB::NewIterator(const lsm::ReadOptions&) { Todo("DiskDB::NewIterator"); }
std::shared_ptr<const lsm::Snapshot> DiskDB::GetSnapshot() { Todo("DiskDB::GetSnapshot"); }
Status DiskDB::FlushMemTable() { Todo("DiskDB::FlushMemTable"); }
Status DiskDB::CompactAll() { Todo("DiskDB::CompactAll"); }
lsm::Stats DiskDB::GetStats() const { Todo("DiskDB::GetStats"); }
std::vector<std::vector<std::pair<std::string, std::string>>> DiskDB::DebugLevelUserRanges() const {
    Todo("DiskDB::DebugLevelUserRanges");
}
std::vector<std::string> DiskDB::DebugLiveFiles() const { Todo("DiskDB::DebugLiveFiles"); }

}  // namespace dlsm
