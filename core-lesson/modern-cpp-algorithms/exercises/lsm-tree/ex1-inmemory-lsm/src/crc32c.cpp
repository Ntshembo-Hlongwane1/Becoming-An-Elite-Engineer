#include "lsm/crc32c.hpp"
#include "lsm/todo.hpp"

namespace lsm::crc32c {

std::uint32_t Extend(std::uint32_t, const char*, std::size_t) { Todo("crc32c::Extend"); }
std::uint32_t Mask(std::uint32_t) { Todo("crc32c::Mask"); }
std::uint32_t Unmask(std::uint32_t) { Todo("crc32c::Unmask"); }

}  // namespace lsm::crc32c
