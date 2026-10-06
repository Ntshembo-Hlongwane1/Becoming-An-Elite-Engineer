#include "lsm/sorted_run.hpp"
#include "lsm/todo.hpp"

namespace lsm {

SortedRunBuilder::SortedRunBuilder(RunOptions options) : options_(options) {}
void SortedRunBuilder::Add(std::string_view, std::string_view) { Todo("SortedRunBuilder::Add"); }
Status SortedRunBuilder::Finish(std::string*) { Todo("SortedRunBuilder::Finish"); }
std::uint64_t SortedRunBuilder::NumEntries() const { Todo("SortedRunBuilder::NumEntries"); }

Status SortedRun::Open(std::shared_ptr<const std::string>, std::uint64_t, std::shared_ptr<const SortedRun>*) {
    Todo("SortedRun::Open");
}
Status SortedRun::Get(std::string_view, SequenceNumber, LookupResult*, std::string*) const { Todo("SortedRun::Get"); }
std::unique_ptr<Iterator> SortedRun::NewIterator() const { Todo("SortedRun::NewIterator"); }
std::string_view SortedRun::smallest() const { Todo("SortedRun::smallest"); }
std::string_view SortedRun::largest() const { Todo("SortedRun::largest"); }
std::uint64_t SortedRun::number() const { Todo("SortedRun::number"); }
std::size_t SortedRun::size_bytes() const { Todo("SortedRun::size_bytes"); }
std::uint64_t SortedRun::bloom_negatives() const { Todo("SortedRun::bloom_negatives"); }
std::uint64_t SortedRun::data_blocks_read() const { Todo("SortedRun::data_blocks_read"); }

}  // namespace lsm
