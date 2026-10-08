#pragma once
// Exercise 1c — a safe length-prefixed record parser. Notes: Lesson 1 §1.5, §1.6.
//
// This is the integer-overflow bug class (§1.6) defended at a parser — the pattern every later
// lesson's on-disk/on-wire code reuses.
//
// Wire format of ONE record at the start of `buf`:
//     [ u32 little-endian length L ][ L bytes of payload ]
//
// parse_record(buf):
//   * returns the payload (a view of L bytes inside buf) on success,
//   * returns std::nullopt if buf has fewer than 4 bytes (no length), OR if the payload would run
//     past the end of buf.
//   * The past-the-end check must be wrap-safe (§1.6). Here L comes from a u32 and is held in a
//     64-bit std::size_t, so `4 + L` happens not to wrap — but write it so it would be correct even
//     if L were accumulated in a 32-bit type: either compare against the remaining space
//     `buf.size() - 4` (safe because you already proved buf.size() >= 4), or use checked_add. State
//     which you used and why it cannot wrap in DECISIONS.md. (This is the habit that matters: on a
//     32-bit target, or with a 32-bit length accumulator, `4 + L > size` is a real overflow bug.)
//   * A length of 0 is valid: an empty payload (a non-null, zero-size span into buf).
//
// bytes_consumed(buf): like parse_record but returns 4 + L (the total record size) on success, so a
//   caller can advance to the next record. Same rejection rules; compute 4 + L without wrapping.
#include <cstddef>
#include <optional>
#include <span>

namespace mm {

std::optional<std::span<const std::byte>> parse_record(std::span<const std::byte> buf);
std::optional<std::size_t> bytes_consumed(std::span<const std::byte> buf);

}  // namespace mm
