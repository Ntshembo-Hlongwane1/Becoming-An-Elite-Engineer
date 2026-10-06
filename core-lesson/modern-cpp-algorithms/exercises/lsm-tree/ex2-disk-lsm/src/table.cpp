#include "dlsm/table.hpp"
#include "lsm/todo.hpp"

namespace dlsm {
using lsm::Todo;

void TableHeader::EncodeTo(std::string&) const { Todo("TableHeader::EncodeTo"); }
Status TableHeader::DecodeFrom(std::string_view) { Todo("TableHeader::DecodeFrom"); }

TableBuilder::TableBuilder(TableOptions options, WritableFile* file, std::uint64_t)
    : options_(options), file_(file) {}
TableBuilder::~TableBuilder() = default;
void TableBuilder::Add(std::string_view, std::string_view) { Todo("TableBuilder::Add"); }
Status TableBuilder::Finish() { Todo("TableBuilder::Finish"); }
void TableBuilder::Abandon() { Todo("TableBuilder::Abandon"); }
Status TableBuilder::status() const { Todo("TableBuilder::status"); }
std::uint64_t TableBuilder::NumEntries() const { Todo("TableBuilder::NumEntries"); }
std::uint64_t TableBuilder::FileSize() const { Todo("TableBuilder::FileSize"); }
std::string_view TableBuilder::smallest() const { Todo("TableBuilder::smallest"); }
std::string_view TableBuilder::largest() const { Todo("TableBuilder::largest"); }

Status Table::Open(std::unique_ptr<RandomAccessFile>, std::uint64_t, std::shared_ptr<const Table>*) { Todo("Table::Open"); }
Status Table::Get(std::string_view, lsm::SequenceNumber, lsm::LookupResult*, std::string*) const { Todo("Table::Get"); }
std::unique_ptr<lsm::Iterator> Table::NewIterator() const { Todo("Table::NewIterator"); }
const TableHeader& Table::header() const { Todo("Table::header"); }
std::vector<lsm::BlockHandle> Table::DataBlockHandles() const { Todo("Table::DataBlockHandles"); }
std::uint64_t Table::file_size() const { Todo("Table::file_size"); }
std::uint64_t Table::data_blocks_read() const { Todo("Table::data_blocks_read"); }
std::uint64_t Table::bloom_negatives() const { Todo("Table::bloom_negatives"); }

}  // namespace dlsm
