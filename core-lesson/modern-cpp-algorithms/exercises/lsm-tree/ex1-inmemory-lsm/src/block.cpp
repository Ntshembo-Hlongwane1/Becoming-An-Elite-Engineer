#include "lsm/block.hpp"
#include "lsm/todo.hpp"

namespace lsm {

BlockBuilder::BlockBuilder(BlockOptions options) : options_(options) { restarts_.push_back(0); }
void BlockBuilder::Add(std::string_view, std::string_view) { Todo("BlockBuilder::Add"); }
std::string_view BlockBuilder::Finish() { Todo("BlockBuilder::Finish"); }
void BlockBuilder::Reset() { Todo("BlockBuilder::Reset"); }
std::size_t BlockBuilder::CurrentSizeEstimate() const { Todo("BlockBuilder::CurrentSizeEstimate"); }
bool BlockBuilder::empty() const { Todo("BlockBuilder::empty"); }

Status BlockReader::Open(std::string_view, CompareFn, std::unique_ptr<BlockReader>*) { Todo("BlockReader::Open"); }
std::uint32_t BlockReader::NumRestarts() const { Todo("BlockReader::NumRestarts"); }
std::unique_ptr<Iterator> BlockReader::NewIterator() const { Todo("BlockReader::NewIterator"); }

}  // namespace lsm
