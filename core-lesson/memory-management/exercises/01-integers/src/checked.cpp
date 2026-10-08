#include "mm/checked.hpp"
#include "mm/todo.hpp"
namespace mm {
std::optional<std::size_t> checked_add(std::size_t, std::size_t) { Todo("checked_add"); }
std::optional<std::size_t> checked_sub(std::size_t, std::size_t) { Todo("checked_sub"); }
std::optional<std::size_t> checked_mul(std::size_t, std::size_t) { Todo("checked_mul"); }
int midpoint(int, int) { Todo("midpoint"); }
bool less_su(std::int64_t, std::uint64_t) { Todo("less_su"); }
}  // namespace mm
