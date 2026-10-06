#pragma once
// M4 — Internal keys. Notes: Part 5 §4; LevelDB db/dbformat.{h,cc} (read after you're green).
//
//   internal_key := user_key bytes ‖ fixed64( (sequence << 8) | type )      (little-endian fixed64)
//   order        := user key ASCENDING (bytewise), then sequence DESCENDING, then type DESCENDING
//
// The ValueType numbers are part of the on-disk format: never renumber them.
#include <cstdint>
#include <string>
#include <string_view>

namespace lsm {

enum class ValueType : std::uint8_t { kDeletion = 0x0, kValue = 0x1 };
// When seeking for (user_key, snapshot) use the HIGHEST type so the seek key sorts before every
// entry with the same user key and sequence <= snapshot.
inline constexpr ValueType kValueTypeForSeek = ValueType::kValue;

using SequenceNumber = std::uint64_t;
inline constexpr SequenceNumber kMaxSequenceNumber = (std::uint64_t{1} << 56) - 1;

enum class LookupResult { kFound, kDeleted, kNotPresent };

using CompareFn = int (*)(std::string_view, std::string_view);

struct ParsedInternalKey {
    std::string_view user_key;
    SequenceNumber sequence = 0;
    ValueType type = ValueType::kValue;
};

std::uint64_t PackSequenceAndType(SequenceNumber seq, ValueType type);
void AppendInternalKey(std::string& dst, std::string_view user_key, SequenceNumber seq, ValueType type);
std::string MakeInternalKey(std::string_view user_key, SequenceNumber seq, ValueType type);

// False if the key is shorter than 8 bytes or the type byte is not a known ValueType.
bool ParseInternalKey(std::string_view internal_key, ParsedInternalKey* result);
std::string_view ExtractUserKey(std::string_view internal_key);  // requires size >= 8

// memcmp order; a proper prefix sorts first. Returns <0, 0, >0.
int BytewiseCompare(std::string_view a, std::string_view b);
// The internal-key order above.
int InternalKeyCompare(std::string_view a, std::string_view b);

// M7 — index-key shortening (Part 6 §6). Bytewise, on USER keys:
// if possible, change *start into a shorter string s with *start <= s < limit.
void FindShortestSeparator(std::string* start, std::string_view limit);
// Change *key into a short string >= *key (used for the last block's index entry).
void FindShortSuccessor(std::string* key);

}  // namespace lsm
