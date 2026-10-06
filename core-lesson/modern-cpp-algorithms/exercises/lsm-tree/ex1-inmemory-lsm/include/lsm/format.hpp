#pragma once
// M7 — Table-level framing shared by Exercise 1 (bytes in a std::string) and Exercise 2 (bytes in
// a file). Notes: Part 6 §4, §6, §8.
//
//   [data block][trailer] … [filter block][trailer] [metaindex block][trailer] [index block][trailer] [footer]
//   trailer    := uint8 type (0 = no compression) | fixed32 masked_crc32c(block_bytes ‖ type)
//   BlockHandle:= varint64 offset | varint64 size          (size EXCLUDES the 5-byte trailer)
//   footer     := metaindex_handle | index_handle | zero padding to 40 bytes | fixed64 magic   (48 bytes)
#include <cstdint>
#include <string>
#include <string_view>

#include "lsm/status.hpp"

namespace lsm {

inline constexpr std::size_t kBlockTrailerSize = 5;
inline constexpr std::uint8_t kNoCompression = 0;
// "MINILSM1" in ASCII, read as a little-endian fixed64. Change it if you change the format.
inline constexpr std::uint64_t kTableMagic = 0x314D534C494E494Dull;

struct BlockHandle {
    static constexpr std::size_t kMaxEncodedLength = 10 + 10;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    void EncodeTo(std::string& dst) const;
    Status DecodeFrom(std::string_view* input);  // consumes from *input
};

struct Footer {
    static constexpr std::size_t kEncodedLength = 2 * BlockHandle::kMaxEncodedLength + 8;  // 48
    BlockHandle metaindex_handle;
    BlockHandle index_handle;
    void EncodeTo(std::string& dst) const;            // appends exactly 48 bytes
    Status DecodeFrom(std::string_view input);       // input must be exactly the last 48 bytes
};

// Append `contents` + trailer to `file`; return where it went.
BlockHandle AppendBlockWithTrailer(std::string& file, std::string_view contents);

// `raw` = block bytes followed by its 5-byte trailer (exactly handle.size + 5 bytes).
// Verify type and CRC; on success *contents views the block bytes inside `raw`.
Status VerifyBlock(std::string_view raw, std::string_view* contents);

}  // namespace lsm
