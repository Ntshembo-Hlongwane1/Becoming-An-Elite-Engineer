#include "mm/codec.hpp"
#include "mm/todo.hpp"
namespace mm {
void put_u16_le(std::uint8_t*, std::uint16_t) { Todo("put_u16_le"); }
void put_u16_be(std::uint8_t*, std::uint16_t) { Todo("put_u16_be"); }
void put_u32_le(std::uint8_t*, std::uint32_t) { Todo("put_u32_le"); }
void put_u32_be(std::uint8_t*, std::uint32_t) { Todo("put_u32_be"); }
void put_u64_le(std::uint8_t*, std::uint64_t) { Todo("put_u64_le"); }
void put_u64_be(std::uint8_t*, std::uint64_t) { Todo("put_u64_be"); }
std::uint16_t get_u16_le(const std::uint8_t*) { Todo("get_u16_le"); }
std::uint16_t get_u16_be(const std::uint8_t*) { Todo("get_u16_be"); }
std::uint32_t get_u32_le(const std::uint8_t*) { Todo("get_u32_le"); }
std::uint32_t get_u32_be(const std::uint8_t*) { Todo("get_u32_be"); }
std::uint64_t get_u64_le(const std::uint8_t*) { Todo("get_u64_le"); }
std::uint64_t get_u64_be(const std::uint8_t*) { Todo("get_u64_be"); }
}  // namespace mm
