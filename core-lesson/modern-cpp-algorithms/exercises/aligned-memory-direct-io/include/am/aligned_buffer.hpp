#pragma once
// Exercise 3 (intermediate) — AlignedBuffer: a move-only RAII owner of aligned, zeroed bytes.
// Notes: Part 4 §3 (every member is justified there), Part 3 §3, §8, Part 7 §4.
//
// Requirements (the tests check every one):
//  * Allocate(size, align):
//      - `align` must be a power of two AND >= sizeof(void*) (posix_memalign's rule, which
//        std::aligned_alloc inherits on POSIX — Part 3 §3/§4). Otherwise std::invalid_argument.
//      - capacity() = size rounded UP to a multiple of align (use Ex1's AlignUp). If that
//        overflows, throw std::bad_alloc. Never call the allocator with a wrapped size.
//      - size == 0 gives an empty buffer: data() == nullptr, capacity() == 0, alignment() == align.
//      - Memory comes from std::aligned_alloc (or Ex2's AlignedMalloc — your choice; write which
//        and why in DECISIONS.md). Out of memory -> std::bad_alloc.
//      - ALL capacity() bytes are zero-filled (Part 7 §4: padding must never leak old heap data).
//  * The destructor releases with the function that matches the allocator (Part 3 §8).
//  * Copy is deleted. Move ctor/assignment are noexcept, leave the source empty (data()==nullptr,
//    size()==0, capacity()==0), release the target's old memory, and survive self-move.
//  * span() covers size() bytes; padded_span() covers capacity() bytes (the whole aligned region,
//    used when writing a zero-padded tail block with O_DIRECT — Part 5 §7).
#include <cstddef>
#include <span>

namespace am {

class AlignedBuffer {
public:
    AlignedBuffer() noexcept = default;
    static AlignedBuffer Allocate(std::size_t size, std::size_t align);

    ~AlignedBuffer();
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;
    AlignedBuffer(AlignedBuffer&& other) noexcept;
    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept;

    char* data() noexcept { return data_; }
    const char* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t alignment() const noexcept { return align_; }
    bool empty() const noexcept { return size_ == 0; }

    std::span<char> span() noexcept { return {data_, size_}; }
    std::span<const char> span() const noexcept { return {data_, size_}; }
    std::span<char> padded_span() noexcept { return {data_, capacity_}; }
    std::span<const char> padded_span() const noexcept { return {data_, capacity_}; }

private:
    // You may change the private members (e.g. hold a unique_ptr<char, FreeDeleter> instead and
    // `= default` the moves — Part 4 §3 (10)). Keep the public API.
    char* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
    std::size_t align_ = 0;
};

}  // namespace am
