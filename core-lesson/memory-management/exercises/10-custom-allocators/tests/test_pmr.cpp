// Exercise 10 — ArenaResource as a std::pmr::memory_resource driving real pmr containers. Lesson 10.4.
#include <memory_resource>
#include <vector>

#include "mm/arena.hpp"
#include "mm/arena_resource.hpp"
#include "minitest.hpp"

using namespace mm;

TEST(pmr_vector_draws_storage_from_the_arena) {
    Arena arena(1 << 16);
    ArenaResource res(arena);
    std::size_t used_before = arena.used();

    std::pmr::vector<int> v{&res};
    for (int i = 0; i < 100; ++i) v.push_back(i);

    for (int i = 0; i < 100; ++i) CHECK_EQ(v[static_cast<std::size_t>(i)], i);  // the container works
    CHECK(arena.used() > used_before);               // ...and it allocated from OUR arena
    CHECK(arena.owns(v.data()));                      // the element buffer lives inside the arena
}

TEST(pmr_vector_of_strings_also_uses_the_resource) {
    Arena arena(1 << 16);
    ArenaResource res(arena);
    std::pmr::vector<std::pmr::string> v{&res};
    v.emplace_back("hello world, a string long enough to need heap storage not SSO");
    v.emplace_back("another reasonably long string that must allocate its own buffer");
    CHECK_EQ(v.size(), std::size_t{2});
    CHECK(arena.owns(v.data()));                      // the vector's array is in the arena
    // the pmr::string's OWN buffer is also drawn from the same resource (uses-allocator construction)
    CHECK(arena.owns(v[0].data()));
}

TEST(do_deallocate_is_a_noop_arena_only_grows) {
    Arena arena(1 << 16);
    ArenaResource res(arena);
    {
        std::pmr::vector<int> v{&res};
        for (int i = 0; i < 500; ++i) v.push_back(i);  // several reallocations happen
    }  // v destroyed -> do_deallocate called many times, all no-ops
    std::size_t after = arena.used();
    CHECK(after > 0);
    // allocate again: the arena keeps growing from where it was (freed space is NOT reclaimed)
    void* p = res.allocate(16, 16);
    CHECK(p != nullptr);
    CHECK(arena.used() > after);                       // monotonic (Lesson 10.2 §2 / 10.4)
}

TEST(do_is_equal_is_identity_for_stateful_resources) {
    Arena a1(1024), a2(1024);
    ArenaResource r1(a1), r2(a2);
    CHECK(r1.is_equal(r1));                             // same object
    CHECK(!r1.is_equal(r2));                            // different arenas can't free each other's memory
}

TEST(allocate_throws_bad_alloc_when_arena_is_full) {
    Arena arena(64);
    ArenaResource res(arena);
    CHECK(res.allocate(32, 16) != nullptr);
    CHECK_THROWS(res.allocate(1000, 16), std::bad_alloc);   // Lesson 10.4: do_allocate throws on failure
}
