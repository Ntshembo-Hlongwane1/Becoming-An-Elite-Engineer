#pragma once 
#include <cstdint>
#include <cstddef>
#include <type_traits>

inline constexpr std::size_t PAGE_SIZE = 4096;

// Page address on disk is its index in the file. Page N lives at byte offest N * 4096.
using page_id_t = std::uint32_t;

// Sentinel for "no page here". Used by the free list terminator, by a leafs
inline constexpr page_id_t INVALID_PAGE_ID = 0xFFFFFFFFu;

inline constexpr page_id_t HEADER_PAGE_ID = 0;


struct alignas(PAGE_SIZE) Page {
    std::byte data[PAGE_SIZE];
};

static_assert(sizeof(Page) == PAGE_SIZE, "Page must be exactlt one page; check for padding");
static_assert(alignof(Page) == PAGE_SIZE, "Page must be page-aligned for unbuffered I/O");
static_assert(std::is_trivially_copyable_v<Page>, "Page must be trivially copyable for memcpy");