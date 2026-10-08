// Exercise 17 — hardened allocator. Lesson 17.
#include <cstdint>
#include <vector>

#include "mm/hardened_allocator.hpp"
#include "minitest.hpp"

using namespace mm;

TEST(allocate_deallocate_roundtrip) {
    HardenedAllocator a(64, 8);
    void* p = a.allocate();
    CHECK(p != nullptr);
    CHECK(a.owns(p));
    CHECK(a.usable_size(p) >= 64);
    std::memset(p, 'A', a.usable_size(p));   // writing the whole payload is fine (within bounds)
    a.deallocate(p);                          // clean free -> no violation
    CHECK_EQ(a.overflow_detections(), std::size_t{0});
}

TEST(pool_exhaustion_returns_nullptr) {
    HardenedAllocator a(16, 3);
    std::vector<void*> ps;
    for (int i = 0; i < 3; ++i) { void* p = a.allocate(); CHECK(p); ps.push_back(p); }
    CHECK(a.allocate() == nullptr);           // exhausted
    for (void* p : ps) a.deallocate(p);
}

// --- DISCRIMINATORS: these fail with the insecure stubs and pass once you implement the primitives ---

TEST(canary_is_secret_and_per_block) {
    HardenedAllocator a(32, 4);
    int x = 0, y = 0;
    CHECK(a.canary_for(&x) != 0);             // not a guessable constant (stub returns 0 -> fails)
    CHECK(a.canary_for(&x) != a.canary_for(&y));  // differs per address -> attacker can't reuse one
}

TEST(safe_linking_obfuscates_the_pointer) {
    HardenedAllocator a(32, 4);
    int target = 0;
    void* slot = reinterpret_cast<void*>(std::uintptr_t{0x55aa000});  // nonzero (>>12) so XOR changes it
    std::uintptr_t enc = a.encode_link(&target, slot);
    CHECK(enc != reinterpret_cast<std::uintptr_t>(&target));          // actually obfuscated (stub: identity -> fails)
    CHECK(a.decode_link(enc, slot) == &target);                       // and round-trips
}

// --- Attacks the hardening must catch (Lesson 7.5 / 17) ---

TEST(canary_catches_heap_overflow) {
    HardenedAllocator a(48, 8);
    auto* p = static_cast<unsigned char*>(a.allocate());
    CHECK(p != nullptr);
    // Overflow one byte past the payload into the trailer canary. NOTE: this is INSIDE the allocator's
    // backing buffer, so AddressSanitizer does NOT see it (Lesson 10.5) — the allocator's own canary
    // must. (We overwrite with attacker-controlled bytes; the canary makes that detectable.)
    p[a.usable_size(p)] = 0x41;
    CHECK_THROWS(a.deallocate(p), HeapViolation);
    CHECK_EQ(a.overflow_detections(), std::size_t{1});
}

TEST(detects_double_free) {
    HardenedAllocator a(16, 8);
    void* p = a.allocate();
    a.deallocate(p);                          // ok
    CHECK_THROWS(a.deallocate(p), HeapViolation);   // double free
    CHECK_EQ(a.double_free_detections(), std::size_t{1});
}

TEST(detects_invalid_free_of_foreign_pointer) {
    HardenedAllocator a(16, 8);
    int stack_var = 0;
    CHECK_THROWS(a.deallocate(&stack_var), HeapViolation);   // not from this allocator
    CHECK_EQ(a.invalid_free_detections(), std::size_t{1});
}

TEST(corrupted_free_list_link_is_rejected) {
    HardenedAllocator a(32, 4);
    void* p = a.allocate();
    a.deallocate(p);                          // p's block is now the free-list head, next_enc stored
    // Simulate an attacker overwriting the freed block's link with a forged (misaligned) value.
    auto* h = reinterpret_cast<HardenedAllocator::Header*>(static_cast<std::byte*>(p) - sizeof(HardenedAllocator::Header));
    h->next_enc = 0xdeadbeef;                 // garbage
    CHECK_THROWS(a.allocate(), HeapViolation); // pops p, decodes next -> garbage -> rejected
    CHECK(a.invalid_free_detections() >= 1);
}
