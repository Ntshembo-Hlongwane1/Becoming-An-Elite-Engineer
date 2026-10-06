#include "lsm/bloom.hpp"
#include "lsm/todo.hpp"

namespace lsm {

std::uint64_t BloomHash(std::string_view) { Todo("BloomHash"); }
void CreateBloomFilter(const std::vector<std::string_view>&, int, std::string*) { Todo("CreateBloomFilter"); }
bool BloomKeyMayMatch(std::string_view, std::string_view) { Todo("BloomKeyMayMatch"); }

}  // namespace lsm
