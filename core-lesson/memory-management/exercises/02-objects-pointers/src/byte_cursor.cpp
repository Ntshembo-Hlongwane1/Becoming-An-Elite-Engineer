#include "mm/byte_cursor.hpp"
#include "mm/todo.hpp"
namespace mm {
std::optional<std::uint8_t>  ByteReader::read_u8()    { Todo("ByteReader::read_u8"); }
std::optional<std::uint16_t> ByteReader::read_u16_le(){ Todo("ByteReader::read_u16_le"); }
std::optional<std::uint32_t> ByteReader::read_u32_le(){ Todo("ByteReader::read_u32_le"); }
std::optional<std::uint64_t> ByteReader::read_u64_le(){ Todo("ByteReader::read_u64_le"); }
std::optional<std::span<const std::byte>> ByteReader::read_bytes(std::size_t) { Todo("ByteReader::read_bytes"); }
std::optional<std::span<const std::byte>> ByteReader::read_length_prefixed()  { Todo("ByteReader::read_length_prefixed"); }

bool ByteWriter::write_u8(std::uint8_t)    { Todo("ByteWriter::write_u8"); }
bool ByteWriter::write_u16_le(std::uint16_t){ Todo("ByteWriter::write_u16_le"); }
bool ByteWriter::write_u32_le(std::uint32_t){ Todo("ByteWriter::write_u32_le"); }
bool ByteWriter::write_u64_le(std::uint64_t){ Todo("ByteWriter::write_u64_le"); }
bool ByteWriter::write_bytes(std::span<const std::byte>) { Todo("ByteWriter::write_bytes"); }
bool ByteWriter::write_length_prefixed(std::span<const std::byte>) { Todo("ByteWriter::write_length_prefixed"); }
}
