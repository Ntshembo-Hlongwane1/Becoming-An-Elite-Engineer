// Exercise 3 — AlignedBuffer. Notes Part 4 §3, Part 7 §4.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "am/aligned_buffer.hpp"
#include "minitest.hpp"

using namespace am;
static bool Aligned(const void* p, std::size_t a) { return reinterpret_cast<std::uintptr_t>(p) % a == 0; }

static_assert(!std::is_copy_constructible_v<AlignedBuffer>, "copy must be deleted (Part 4 §3 (4))");
static_assert(!std::is_copy_assignable_v<AlignedBuffer>, "copy must be deleted (Part 4 §3 (4))");
static_assert(std::is_nothrow_move_constructible_v<AlignedBuffer>, "move must be noexcept (Part 4 §3 (5))");
static_assert(std::is_nothrow_move_assignable_v<AlignedBuffer>, "move must be noexcept (Part 4 §3 (6))");

TEST(ex3_allocate_aligned_sizes_and_capacity) {
    for (std::size_t align : {8ul, 16ul, 64ul, 512ul, 4096ul, 65536ul}) {
        auto b = AlignedBuffer::Allocate(1000, align);
        CHECK(b.data() != nullptr);
        CHECK(Aligned(b.data(), align));
        CHECK_EQ(b.size(), std::size_t{1000});
        CHECK_EQ(b.alignment(), align);
        CHECK(b.capacity() >= 1000);
        CHECK_EQ(b.capacity() % align, std::size_t{0});
        CHECK(b.capacity() < 1000 + align);   // rounded up, not wasted
        std::memset(b.data(), 1, b.capacity());  // whole capacity is ours (ASan)
    }
    auto exact = AlignedBuffer::Allocate(4096, 512);
    CHECK_EQ(exact.capacity(), std::size_t{4096});
}

TEST(ex3_zero_filled_including_padding) {
    // Part 7 §4: dirty the heap first so an un-zeroed buffer would very likely show garbage.
    for (int i = 0; i < 64; ++i) {
        void* p = std::aligned_alloc(512, 4096);
        std::memset(p, 0x5A, 4096);
        std::free(p);
    }
    for (int i = 0; i < 64; ++i) {
        auto b = AlignedBuffer::Allocate(100 + std::size_t(i), 512);
        for (char c : b.padded_span()) CHECK_EQ(c, '\0');
    }
}

TEST(ex3_spans) {
    auto b = AlignedBuffer::Allocate(700, 512);
    CHECK_EQ(b.span().size(), std::size_t{700});
    CHECK_EQ(b.padded_span().size(), std::size_t{1024});
    CHECK(b.span().data() == b.data());
    const AlignedBuffer& cb = b;
    CHECK_EQ(cb.span().size(), std::size_t{700});
    CHECK(cb.padded_span().data() == cb.data());
}

TEST(ex3_invalid_alignment_throws_invalid_argument) {
    CHECK_THROWS(AlignedBuffer::Allocate(64, 0), std::invalid_argument);
    CHECK_THROWS(AlignedBuffer::Allocate(64, 3), std::invalid_argument);
    CHECK_THROWS(AlignedBuffer::Allocate(64, 3000), std::invalid_argument);
    CHECK_THROWS(AlignedBuffer::Allocate(64, 4), std::invalid_argument);   // < sizeof(void*)
}

TEST(ex3_overflow_throws_bad_alloc) {
    constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();
    CHECK_THROWS(AlignedBuffer::Allocate(kMax - 10, 4096), std::bad_alloc);
    CHECK_THROWS(AlignedBuffer::Allocate(kMax, 512), std::bad_alloc);
}

TEST(ex3_size_zero_is_empty) {
    auto b = AlignedBuffer::Allocate(0, 512);
    CHECK(b.empty());
    CHECK(b.data() == nullptr);
    CHECK_EQ(b.capacity(), std::size_t{0});
    CHECK_EQ(b.alignment(), std::size_t{512});
    AlignedBuffer d;   // default-constructed is empty too
    CHECK(d.empty() && d.data() == nullptr && d.capacity() == 0);
}

TEST(ex3_move_construct_transfers_ownership) {
    auto a = AlignedBuffer::Allocate(4096, 512);
    a.data()[0] = 'Q';
    char* raw = a.data();
    AlignedBuffer b(std::move(a));
    CHECK(b.data() == raw);
    CHECK_EQ(b.data()[0], 'Q');
    CHECK_EQ(b.size(), std::size_t{4096});
    CHECK(a.data() == nullptr);   // NOLINT: testing moved-from state on purpose
    CHECK_EQ(a.size(), std::size_t{0});
    CHECK_EQ(a.capacity(), std::size_t{0});
}   // ASan: exactly one free here, no double free

TEST(ex3_move_assign_releases_old_and_steals) {
    auto a = AlignedBuffer::Allocate(4096, 512);
    auto b = AlignedBuffer::Allocate(100, 64);   // b's old memory must be released (ASan leak check)
    char* raw = a.data();
    b = std::move(a);
    CHECK(b.data() == raw);
    CHECK_EQ(b.alignment(), std::size_t{512});
    CHECK(a.data() == nullptr);   // NOLINT
}

TEST(ex3_self_move_assignment_is_safe) {
    auto b = AlignedBuffer::Allocate(4096, 512);
    b.data()[10] = 'Z';
    AlignedBuffer& alias = b;          // alias avoids -Wself-move; the code path is the same
    b = std::move(alias);
    CHECK(b.data() != nullptr);
    CHECK_EQ(b.data()[10], 'Z');       // ASan: heap-use-after-free here if you freed first
    CHECK_EQ(b.size(), std::size_t{4096});
}

TEST(ex3_lives_in_a_vector) {
    std::vector<AlignedBuffer> v;
    for (int i = 0; i < 200; ++i) {
        v.push_back(AlignedBuffer::Allocate(512, 512));
        v.back().data()[0] = static_cast<char>(i);
    }
    for (int i = 0; i < 200; ++i) {
        CHECK(Aligned(v[std::size_t(i)].data(), 512));
        CHECK_EQ(v[std::size_t(i)].data()[0], static_cast<char>(i));
    }
}
