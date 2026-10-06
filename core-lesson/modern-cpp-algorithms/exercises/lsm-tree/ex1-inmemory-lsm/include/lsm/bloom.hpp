#pragma once
// M6 — Bloom filter. Notes: Part 3 §7; LevelDB util/bloom.cc (read after you're green).
//
// Filter bytes := bit array (>= 64 bits, whole bytes) ‖ uint8 k
// Hash: a SPECIFIED, deterministic function of the key bytes only (document which one).
// Probes: double hashing  h, h+delta, h+2·delta, …   k = clamp(floor(bits_per_key * 0.69), 1, 30).
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lsm {

std::uint64_t BloomHash(std::string_view key);

// Appends a filter for `keys` to *dst.
void CreateBloomFilter(const std::vector<std::string_view>& keys, int bits_per_key, std::string* dst);

// false => key is definitely not in the set. Must use the k stored in the filter.
// A filter shorter than 2 bytes matches nothing; a stored k > 30 matches everything (reserved).
bool BloomKeyMayMatch(std::string_view key, std::string_view filter);

}  // namespace lsm
