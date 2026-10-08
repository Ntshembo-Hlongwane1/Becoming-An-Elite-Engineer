#include "mm/record.hpp"
#include "mm/todo.hpp"
namespace mm {
std::optional<std::span<const std::byte>> parse_record(std::span<const std::byte>) { Todo("parse_record"); }
std::optional<std::size_t> bytes_consumed(std::span<const std::byte>) { Todo("bytes_consumed"); }
}  // namespace mm
