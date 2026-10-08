// Exercise 5 — stack prober + backtrace. Lesson 5.
#include <cstddef>
#include <cstdint>
#include <unistd.h>
#include <vector>

#include "mm/stack.hpp"
#include "minitest.hpp"

using namespace mm;

TEST(stack_grows_down_on_x86_64) {
    CHECK(stack_direction() == StackDirection::kDown);
}

TEST(stack_limit_is_sane) {
    std::size_t lim = stack_limit_bytes();
    // 0 means "unlimited"; otherwise it should be a sane, page-multiple size (>= 256 KiB).
    if (lim != 0) {
        CHECK(lim >= std::size_t{256} * 1024);
        long ps = sysconf(_SC_PAGESIZE);
        CHECK_EQ(lim % static_cast<std::size_t>(ps), std::size_t{0});
    }
}

TEST(adjacent_frame_delta_is_negative) {
    std::ptrdiff_t d = adjacent_frame_delta();
    CHECK(d < 0);                       // inner frame is lower (grows down)
    CHECK(-d >= 8);                     // at least a few bytes of frame
    CHECK(-d < 1'000'000);              // but not absurd
}

// Helpers at known nesting depths for the backtrace test. noinline so the frames really exist even
// if someone builds optimized.
__attribute__((noinline)) static std::size_t bt_leaf(std::span<void*> out) { return capture_backtrace(out); }
__attribute__((noinline)) static std::size_t bt_mid(std::span<void*> out)  { auto n = bt_leaf(out); asm volatile("" ::: "memory"); return n; }
__attribute__((noinline)) static std::size_t bt_deep(std::span<void*> out) { auto n = bt_mid(out);  asm volatile("" ::: "memory"); return n; }

TEST(backtrace_basic) {
    void* buf[64];
    std::size_t n = capture_backtrace(buf);
    CHECK(n >= 2);                      // at least this test frame + the runner
    CHECK(n <= 64);
    for (std::size_t i = 0; i < n; ++i) CHECK(buf[i] != nullptr);
}

TEST(backtrace_depth_increases_with_nesting) {
    void* a[64];
    void* b[64];
    std::size_t shallow = capture_backtrace(a);
    std::size_t deep = bt_deep(b);      // three extra frames: bt_deep -> bt_mid -> bt_leaf
    CHECK(deep > shallow);
    CHECK(deep - shallow >= 3);         // the three helper frames show up
}

TEST(backtrace_respects_capacity) {
    void* small[2];
    std::size_t n = bt_deep(std::span<void*>(small, 2));
    CHECK(n <= 2);                      // never writes more than the span holds
}
