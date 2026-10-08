#pragma once
// Exercise 2 — a bounds-checked cursor over raw storage. Notes: Lesson 2 (all), reuses Lesson 1 codec.
//
// This is the defence from Lesson 2.6 in miniature: a pointer that CANNOT walk out of its buffer,
// because every read/write checks the length first — with Lesson 1's overflow-safe arithmetic.
//
// Contracts (the tests are the spec):
//  * ByteReader reads from immutable bytes; ByteWriter writes into mutable bytes. Both hold a
//    std::span (pointer + length, Lesson 2.2 §5) and a position that starts at 0.
//  * Every read_*/write_* checks there is room for the whole item BEFORE touching memory and BEFORE
//    advancing. A read that doesn't fit returns std::nullopt and leaves the position UNCHANGED; a
//    write that doesn't fit returns false and leaves the position unchanged. The cursor can never
//    form or dereference an out-of-range pointer (Lesson 2.3).
//  * The "is there room for n more bytes" check must be wrap-safe: compare `n > remaining()`, never
//    `position() + n > size()` (which can wrap when n is near SIZE_MAX — Lesson 1.3). A test feeds a
//    huge n to catch a wrapping check.
//  * Integers use Lesson 1's little-endian codec (mm/codec.hpp), which is provided here.
//  * read_object<T>/write_object<T> move a trivially-copyable T to/from the buffer with std::memcpy
//    (the aliasing-safe pun, Lesson 2.4 §3) — never a reinterpret_cast of the buffer. They are
//    bounds-checked like everything else. A static_assert rejects non-trivially-copyable T.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>

namespace mm {

class ByteReader {
public:
    explicit ByteReader(std::span<const std::byte> buf) noexcept : buf_(buf) {}

    std::size_t position() const noexcept { return pos_; }
    std::size_t size() const noexcept { return buf_.size(); }
    std::size_t remaining() const noexcept { return buf_.size() - pos_; }   // pos_ <= size() always
    bool eof() const noexcept { return pos_ == buf_.size(); }

    std::optional<std::uint8_t>  read_u8();
    std::optional<std::uint16_t> read_u16_le();
    std::optional<std::uint32_t> read_u32_le();
    std::optional<std::uint64_t> read_u64_le();

    // A view of the next n bytes (no copy), advancing past them; nullopt if fewer than n remain.
    std::optional<std::span<const std::byte>> read_bytes(std::size_t n);
    // [u32 LE length][length bytes]: returns the payload view, advancing past both.
    std::optional<std::span<const std::byte>> read_length_prefixed();

    template <class T>
    std::optional<T> read_object() {
        static_assert(std::is_trivially_copyable_v<T>, "read_object<T> requires trivially-copyable T");
        auto s = read_bytes(sizeof(T));
        if (!s) return std::nullopt;
        T out;
        std::memcpy(&out, s->data(), sizeof(T));   // aliasing-safe (Lesson 2.4)
        return out;
    }

private:
    std::span<const std::byte> buf_;
    std::size_t pos_ = 0;
};

class ByteWriter {
public:
    explicit ByteWriter(std::span<std::byte> buf) noexcept : buf_(buf) {}

    std::size_t position() const noexcept { return pos_; }
    std::size_t size() const noexcept { return buf_.size(); }
    std::size_t remaining() const noexcept { return buf_.size() - pos_; }
    std::span<const std::byte> written() const noexcept { return buf_.subspan(0, pos_); }

    bool write_u8(std::uint8_t v);
    bool write_u16_le(std::uint16_t v);
    bool write_u32_le(std::uint32_t v);
    bool write_u64_le(std::uint64_t v);
    bool write_bytes(std::span<const std::byte> src);
    bool write_length_prefixed(std::span<const std::byte> payload);   // u32 LE length then payload

    template <class T>
    bool write_object(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>, "write_object<T> requires trivially-copyable T");
        std::byte tmp[sizeof(T)];
        std::memcpy(tmp, &v, sizeof(T));           // aliasing-safe
        return write_bytes(std::span<const std::byte>(tmp, sizeof(T)));
    }

private:
    std::span<std::byte> buf_;
    std::size_t pos_ = 0;
};

}  // namespace mm
