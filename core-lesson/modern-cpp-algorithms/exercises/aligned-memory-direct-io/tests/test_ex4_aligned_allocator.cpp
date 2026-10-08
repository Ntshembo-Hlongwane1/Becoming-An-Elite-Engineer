// Exercise 4 — AlignedAllocator<T, Align>. Notes Part 4 §4–§5.
#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <new>
#include <type_traits>
#include <vector>

#include "am/aligned_allocator.hpp"
#include "minitest.hpp"

using namespace am;
static bool Aligned(const void* p, std::size_t a) { return reinterpret_cast<std::uintptr_t>(p) % a == 0; }

struct alignas(128) Wide {
    double x[16];
};

TEST(ex4_alignment_never_weaker_than_type) {
    CHECK_EQ((AlignedAllocator<char, 4096>::alignment), std::size_t{4096});
    CHECK_EQ((AlignedAllocator<double, 1>::alignment), alignof(double));
    CHECK_EQ((AlignedAllocator<Wide, 64>::alignment), std::size_t{128});
    CHECK_EQ((AlignedAllocator<std::max_align_t, 8>::alignment), alignof(std::max_align_t));
}

TEST(ex4_traits_and_rebind) {
    using A = AlignedAllocator<int, 512>;
    using Tr = std::allocator_traits<A>;
    CHECK((std::is_same_v<Tr::rebind_alloc<long>, AlignedAllocator<long, 512>>));
    CHECK(Tr::is_always_equal::value);   // stateless (Part 4 §4 (4))
    A a;
    AlignedAllocator<long, 512> b(a);    // converting ctor
    CHECK(a == b);
    CHECK(!(a != b));
}

TEST(ex4_vector_stays_aligned_through_growth) {
    std::vector<char, AlignedAllocator<char, 4096>> v;
    for (int i = 0; i < 100000; ++i) {
        v.push_back(static_cast<char>(i));
        CHECK(Aligned(v.data(), 4096));   // every reallocation
    }
    for (int i = 0; i < 100000; ++i) CHECK_EQ(v[std::size_t(i)], static_cast<char>(i));
    v.shrink_to_fit();
    CHECK(Aligned(v.data(), 4096));
}

TEST(ex4_vector_of_doubles_and_overaligned) {
    std::vector<double, AlignedAllocator<double, 64>> d(1000, 1.5);
    CHECK(Aligned(d.data(), 64));
    std::vector<Wide, AlignedAllocator<Wide, 64>> w(10);
    CHECK(Aligned(w.data(), 128));
    for (auto& e : w) CHECK(Aligned(&e, 128));
}

TEST(ex4_node_containers_compile_and_work) {
    // These need rebind + converting ctor: the container allocates NODES, not ints (Part 4 §5).
    std::list<int, AlignedAllocator<int, 256>> l;
    for (int i = 0; i < 1000; ++i) l.push_back(i);
    int expect = 0;
    for (int x : l) CHECK_EQ(x, expect++);
    std::map<int, int, std::less<int>, AlignedAllocator<std::pair<const int, int>, 64>> m;
    for (int i = 0; i < 1000; ++i) m[i] = i * 2;
    CHECK_EQ(m[500], 1000);
}

TEST(ex4_swap_and_move_need_equality) {
    // The book's CustomAllocator fails to compile here (Part 3 §9.4). Yours must compile AND work.
    std::vector<int, AlignedAllocator<int, 512>> a(10, 1), b(20, 2);
    a.swap(b);
    CHECK_EQ(a.size(), std::size_t{20});
    CHECK(Aligned(a.data(), 512));
    std::vector<int, AlignedAllocator<int, 512>> c;
    c = std::move(a);
    CHECK_EQ(c.size(), std::size_t{20});
    CHECK(Aligned(c.data(), 512));
}

TEST(ex4_allocate_overflow_throws_bad_array_new_length) {
    AlignedAllocator<std::uint64_t, 64> a;
    constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();
    CHECK_THROWS(a.allocate(kMax / 4), std::bad_array_new_length);   // kMax/4 * 8 overflows
    CHECK_THROWS(a.allocate(kMax), std::bad_array_new_length);
}

TEST(ex4_direct_allocate_deallocate) {
    AlignedAllocator<char, 512> a;
    char* p = a.allocate(4096);
    CHECK(Aligned(p, 512));
    for (int i = 0; i < 4096; ++i) p[i] = 'x';   // ASan: all ours
    a.deallocate(p, 4096);                       // ASan: new-delete-type-mismatch if not the aligned delete
}
