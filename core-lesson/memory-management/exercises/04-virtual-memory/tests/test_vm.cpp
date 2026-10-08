// Exercise 4 — virtual memory primitives. Lesson 4.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "mm/vm.hpp"
#include "minitest.hpp"

using namespace mm;
using U = std::uintptr_t;

TEST(page_size_is_power_of_two) {
    std::size_t ps = page_size();
    CHECK(ps >= 4096);
    CHECK((ps & (ps - 1)) == 0);   // power of two
}

TEST(page_base_and_offset) {
    const std::size_t ps = 4096;
    CHECK_EQ(page_base(0x12345, ps), U{0x12000});
    CHECK_EQ(page_offset(0x12345, ps), std::size_t{0x345});
    CHECK_EQ(page_base(0x12000, ps), U{0x12000});   // already aligned
    CHECK_EQ(page_offset(0x12000, ps), std::size_t{0});
    CHECK_EQ(page_base(ps - 1, ps), U{0});
    CHECK_EQ(page_offset(ps - 1, ps), ps - 1);
    // base + offset == addr, always
    for (U a : {U{0}, U{1}, U{4095}, U{4096}, U{4097}, U{0xdeadbeef}}) {
        CHECK_EQ(page_base(a, ps) + page_offset(a, ps), a);
    }
}

TEST(pages_spanned_cases) {
    const std::size_t ps = 4096;
    CHECK_EQ(pages_spanned(0, 0, ps), std::size_t{0});           // empty
    CHECK_EQ(pages_spanned(0, 1, ps), std::size_t{1});
    CHECK_EQ(pages_spanned(0, ps, ps), std::size_t{1});          // exactly one page
    CHECK_EQ(pages_spanned(0, ps + 1, ps), std::size_t{2});
    CHECK_EQ(pages_spanned(ps - 1, 1, ps), std::size_t{1});      // last byte of page 0
    CHECK_EQ(pages_spanned(ps - 1, 2, ps), std::size_t{2});      // straddles the boundary
    CHECK_EQ(pages_spanned(ps - 1, ps, ps), std::size_t{2});     // 4096 bytes but misaligned -> 2
    CHECK_EQ(pages_spanned(100, 3 * ps, ps), std::size_t{4});    // offset 100 pushes into a 4th page
    CHECK_EQ(pages_spanned(2 * ps, ps, ps), std::size_t{1});     // aligned block
}

TEST(parse_status_kb_basic) {
    std::string s =
        "Name:\tprog\n"
        "VmPeak:\t   20000 kB\n"
        "VmSize:\t   15360 kB\n"
        "VmRSS:\t    12108 kB\n"
        "Threads:\t1\n";
    CHECK(parse_status_kb(s, "VmRSS")  == std::optional<long>{12108});
    CHECK(parse_status_kb(s, "VmSize") == std::optional<long>{15360});
    CHECK(parse_status_kb(s, "VmPeak") == std::optional<long>{20000});
    CHECK(!parse_status_kb(s, "VmHWM").has_value());        // absent
    CHECK(!parse_status_kb(s, "Name").has_value());         // present but not a "N kB" value
    CHECK(!parse_status_kb(s, "Vm").has_value());           // partial key must not match VmPeak
    CHECK(!parse_status_kb(s, "RSS").has_value());          // suffix must not match VmRSS
    CHECK(!parse_status_kb("VmRSS 999 kB\n", "VmRSS").has_value());   // no colon -> malformed, reject
    CHECK(parse_status_kb("VmRSS:\t999 kB\n", "VmRSS") == std::optional<long>{999});
}

TEST(parse_status_kb_real_self) {
    std::string s;
    if (FILE* f = std::fopen("/proc/self/status", "rb")) {
        char b[4096]; size_t n;
        while ((n = std::fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
        std::fclose(f);
    }
    if (s.empty()) SKIP("/proc/self/status unavailable");
    auto rss = parse_status_kb(s, "VmRSS");
    auto vsz = parse_status_kb(s, "VmSize");
    CHECK(rss.has_value() && *rss > 0);
    CHECK(vsz.has_value() && *vsz > 0);
    CHECK(*vsz >= *rss);          // virtual size is at least resident size (Lesson 4.2 §4)
}

TEST(probe_demand_paging) {
    const std::size_t N = 512;                 // 512 pages ~= 2 MiB of virtual space
    PageProbe probe(N);
    // Freshly mmap'd anonymous pages are NOT resident (Lesson 4.2 §2): lazy allocation.
    CHECK_EQ(probe.resident_count(), std::size_t{0});
    CHECK(!probe.is_resident(0));
    CHECK(!probe.is_resident(N - 1));

    probe.touch(10);
    probe.touch(20);
    probe.touch(N - 1);
    CHECK(probe.is_resident(10));
    CHECK(probe.is_resident(20));
    CHECK(probe.is_resident(N - 1));
    CHECK(!probe.is_resident(11));             // untouched neighbour stays unbacked
    CHECK_EQ(probe.resident_count(), std::size_t{3});
}

TEST(probe_touch_all) {
    const std::size_t N = 64;
    PageProbe probe(N);
    CHECK_EQ(probe.resident_count(), std::size_t{0});
    for (std::size_t i = 0; i < N; ++i) probe.touch(i);
    CHECK_EQ(probe.resident_count(), N);
}

TEST(probe_destructor_unmaps) {
    // LSan does not track mmap, so a missing munmap is invisible to the leak detector. Catch it via
    // virtual size: create+destroy a large mapping many times; if the destructor forgets munmap,
    // VmSize balloons. Each probe is 4096 pages = 16 MiB; 64 iterations would leak ~1 GiB of VSZ.
    std::string s;
    if (FILE* f = std::fopen("/proc/self/status", "rb")) {
        char b[4096]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
        std::fclose(f);
    }
    if (s.empty()) SKIP("/proc/self/status unavailable");
    long vsz_before = parse_status_kb(s, "VmSize").value_or(-1);
    CHECK(vsz_before > 0);
    const std::size_t pages = 4096;                 // 16 MiB per probe
    for (int i = 0; i < 64; ++i) { PageProbe p(pages); p.touch(0); }
    std::string s2;
    if (FILE* f = std::fopen("/proc/self/status", "rb")) {
        char b[4096]; size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) s2.append(b, n);
        std::fclose(f);
    }
    long vsz_after = parse_status_kb(s2, "VmSize").value_or(-1);
    // If unmapped correctly, VSZ is roughly flat. Allow 200 MiB slack; a leak would be ~1 GiB.
    CHECK(vsz_after - vsz_before < 200 * 1024);
}
