#include "lsm/memtable.hpp"
#include "lsm/todo.hpp"

namespace lsm {

int MemTable::EntryComparator::operator()(const char*, const char*) const {
    Todo("MemTable::EntryComparator");
}

MemTable::MemTable() : table_(EntryComparator{}, &arena_) {}

void MemTable::Add(SequenceNumber, ValueType, std::string_view, std::string_view) { Todo("MemTable::Add"); }
LookupResult MemTable::Get(std::string_view, SequenceNumber, std::string*) const { Todo("MemTable::Get"); }
std::size_t MemTable::ApproximateMemoryUsage() const { Todo("MemTable::ApproximateMemoryUsage"); }
std::unique_ptr<Iterator> MemTable::NewIterator() const { Todo("MemTable::NewIterator"); }

}  // namespace lsm
