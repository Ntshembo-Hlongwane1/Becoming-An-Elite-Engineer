#pragma once
// Exercise 4 — virtual-memory primitives. Notes: Lesson 4.
//
// Three groups, each a thing your capstone needs:
//  (a) page arithmetic  — split/round addresses to pages (Lesson 4.1 §2; same masks as Lesson 1).
//  (b) /proc/self/status parsing — read VmRSS/VmSize etc. (Lesson 4.2 §4).
//  (c) a demand-paging prober using mincore — prove lazy allocation (Lesson 4.2 §5).
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace mm {

// (a) PAGE ARITHMETIC. page_size is a power of two (4096 here). Implement with masks (Lesson 1 §7-8).
std::size_t page_size();                                           // sysconf(_SC_PAGESIZE)
std::uintptr_t page_base(std::uintptr_t addr, std::size_t ps);     // addr rounded DOWN to a page
std::size_t page_offset(std::uintptr_t addr, std::size_t ps);      // addr within its page [0, ps)
// Number of pages a byte range [addr, addr+len) touches (0 if len==0). A range can straddle one more
// page than len/ps suggests, e.g. 1 byte at offset ps-1 spans 2 pages. Derive it with page_base.
std::size_t pages_spanned(std::uintptr_t addr, std::size_t len, std::size_t ps);

// (b) /proc/<pid>/status parsing. Lines look like "VmRSS:\t   12108 kB". Return the number of kB for
// the given key (without the colon), or nullopt if the key is absent/malformed. Keys are unique.
std::optional<long> parse_status_kb(std::string_view status_text, std::string_view key);

// (c) DEMAND-PAGING PROBER. Wraps an anonymous mmap and mincore (Lesson 4.2 §5).
class PageProbe {
public:
    explicit PageProbe(std::size_t num_pages);   // mmap num_pages anonymous rw- pages; throw on failure
    ~PageProbe();
    PageProbe(const PageProbe&) = delete;
    PageProbe& operator=(const PageProbe&) = delete;

    std::size_t num_pages() const noexcept { return num_pages_; }
    unsigned char* data() const noexcept { return base_; }

    void touch(std::size_t page_index);          // write one byte in page `page_index`
    bool is_resident(std::size_t page_index) const;  // mincore: is that page backed by a frame?
    std::size_t resident_count() const;          // how many of the pages are resident now

private:
    unsigned char* base_ = nullptr;
    std::size_t num_pages_ = 0;
    std::size_t ps_ = 0;
};

}  // namespace mm
