#include "lsm/memdb.hpp"
#include "lsm/todo.hpp"

namespace lsm {

MemDB::MemDB(Options options) : options_(options) {}
MemDB::~MemDB() = default;

void MemDB::Put(std::string_view, std::string_view) { Todo("MemDB::Put"); }
void MemDB::Delete(std::string_view) { Todo("MemDB::Delete"); }
void MemDB::Write(const WriteBatch&) { Todo("MemDB::Write"); }
Status MemDB::Get(const ReadOptions&, std::string_view, std::string*) { Todo("MemDB::Get"); }
std::unique_ptr<Iterator> MemDB::NewIterator(const ReadOptions&) { Todo("MemDB::NewIterator"); }
std::shared_ptr<const Snapshot> MemDB::GetSnapshot() { Todo("MemDB::GetSnapshot"); }
void MemDB::FlushMemTable() { Todo("MemDB::FlushMemTable"); }
void MemDB::CompactAll() { Todo("MemDB::CompactAll"); }
SequenceNumber MemDB::LastSequence() const { Todo("MemDB::LastSequence"); }
Stats MemDB::GetStats() const { Todo("MemDB::GetStats"); }
std::string MemDB::DebugString() const { Todo("MemDB::DebugString"); }
std::vector<std::vector<std::pair<std::string, std::string>>> MemDB::DebugLevelUserRanges() const {
    Todo("MemDB::DebugLevelUserRanges");
}

}  // namespace lsm
