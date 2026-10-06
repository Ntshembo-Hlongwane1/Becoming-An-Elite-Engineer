#pragma once
// M1 — Byte encoding. Notes: Part 2 §4 (endianness), §5 (aliasing), §6 (varints).
//
// Rules you must follow (each is a lesson):
//  * Fixed-width integers are LITTLE-ENDIAN on disk regardless of the CPU. Encode/decode with
//    shifts on values (like LevelDB's EncodeFixed32), never by casting a char* to uint32_t*.
//  * Decoders take a `limit` and never read at or past it. Return nullptr on truncated input AND on
//    over-long varints (more than 5 bytes for 32-bit, 10 bytes for 64-bit).
//  * Varint format = Protocol Buffers base-128 varint [PB-ENC]: 7 payload bits per byte, low
//    groups first, MSB = "more bytes follow". Example: 150 -> 0x96 0x01.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lsm {

void PutFixed32(std::string& dst, std::uint32_t value);
void PutFixed64(std::string& dst, std::uint64_t value);
void EncodeFixed32(char* dst, std::uint32_t value);   // writes exactly 4 bytes
void EncodeFixed64(char* dst, std::uint64_t value);   // writes exactly 8 bytes
std::uint32_t DecodeFixed32(const char* ptr);          // caller guarantees 4 readable bytes
std::uint64_t DecodeFixed64(const char* ptr);          // caller guarantees 8 readable bytes

void PutVarint32(std::string& dst, std::uint32_t value);
void PutVarint64(std::string& dst, std::uint64_t value);
int VarintLength(std::uint64_t value);                 // number of bytes PutVarint64 would emit

// Decode a varint from [p, limit). On success store it in *value and return the pointer just past
// the varint. On truncation or over-long encoding return nullptr and leave *value unspecified.
const char* GetVarint32Ptr(const char* p, const char* limit, std::uint32_t* value);
const char* GetVarint64Ptr(const char* p, const char* limit, std::uint64_t* value);

// Consuming variants: on success advance *input past the varint and return true.
bool GetVarint32(std::string_view* input, std::uint32_t* value);
bool GetVarint64(std::string_view* input, std::uint64_t* value);

// Length-prefixed string: varint32 length followed by that many bytes.
void PutLengthPrefixed(std::string& dst, std::string_view value);
// On success, *result views bytes inside *input (no copy) and *input is advanced past them.
// Fails (returns false) if the claimed length exceeds the remaining bytes.
bool GetLengthPrefixed(std::string_view* input, std::string_view* result);

}  // namespace lsm
