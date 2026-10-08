#pragma once
// Exercise 17 — harden a free-list allocator. Notes: Lesson 17. Builds on Lesson 7 (free-list
// allocator) and Lesson 7.5 (the attacks: overflow corrupts metadata, double-free, free-list
// pointer injection). You add the defences a modern allocator ships:
//   * per-block CANARIES around the user payload  -> detect heap overflow (ASan can't see inside an
//     arena carved from one big buffer — Lesson 10.5 — so the allocator must check itself);
//   * SAFE-LINKING of the intrusive free-list pointer (glibc 2.32) -> an attacker who overwrites a
//     freed block's "next" can't inject a usable pointer without the per-run secret;
//   * double-/invalid-free detection via a state magic; poison-on-free.
//
// YOU IMPLEMENT the security primitives (marked below): canary_for, arm_canaries, canaries_intact,
// encode_link, decode_link. The block layout, the backing buffer, allocate/deallocate plumbing, the
// detection policy (throw HeapViolation + bump a counter), and the random secret are PROVIDED — read
// them. The provided stubs are FUNCTIONAL but INSECURE (constant canary, identity "encoding"); the
// tests fail until you make the canary secret-derived and the link actually obfuscated.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <stdexcept>

namespace mm {

struct HeapViolation : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class HardenedAllocator {
  public:
    static constexpr std::uint64_t kAllocMagic = 0xA110CA7EDULL;  // "allocated"
    static constexpr std::uint64_t kFreeMagic  = 0xF4EEEEEEDULL;  // "freed"

    // Per-block metadata (PROVIDED). Declared up here because the primitives below take Header*.
    struct Header {
        std::uint64_t state;        // kAllocMagic / kFreeMagic (double-free detection)
        std::uint64_t canary;       // header canary (your arm/check)
        std::uintptr_t next_enc;    // safe-linked free-list next (only meaningful when free)
    };

    HardenedAllocator(std::size_t block_payload, std::size_t block_count)
        : payload_(align16(block_payload < 16 ? 16 : block_payload)),
          block_(sizeof(Header) + payload_ + sizeof(std::uint64_t)),   // header + payload + trailer canary
          count_(block_count),
          base_(static_cast<std::byte*>(::operator new(block_ * block_count, std::align_val_t(16)))) {
        std::random_device rd;
        secret_ = (std::uint64_t(rd()) << 32) ^ rd() ^ 0x9E3779B97F4A7C15ULL;
        secret_ |= 1;                                   // never zero
        free_head_ = nullptr;
        for (std::size_t i = count_; i-- > 0;) {         // build the free list (safe-linked)
            Header* h = header_at(i);
            h->state = kFreeMagic;
            fl_push(h);
        }
    }
    ~HardenedAllocator() { ::operator delete(base_, std::align_val_t(16)); }
    HardenedAllocator(const HardenedAllocator&) = delete;
    HardenedAllocator& operator=(const HardenedAllocator&) = delete;

    // ---------- PROVIDED: allocate / deallocate (call your primitives) ----------
    void* allocate() {
        if (!free_head_) return nullptr;                 // pool exhausted
        Header* h = free_head_;
        free_head_ = fl_decode_next(h);                  // decode the safe-linked next (your decode_link)
        if (free_head_ && !in_range(free_head_)) {       // a corrupted link decodes to a bad pointer
            ++invalid_free_; free_head_ = nullptr;
            throw HeapViolation("corrupted free-list link");
        }
        h->state = kAllocMagic;
        arm_canaries(h);                                 // your canaries
        return payload_of(h);
    }

    void deallocate(void* p) {
        if (!p) return;
        if (!in_range_payload(p)) { ++invalid_free_; throw HeapViolation("invalid free (foreign pointer)"); }
        Header* h = header_from_payload(p);
        if (h->state == kFreeMagic) { ++double_free_; throw HeapViolation("double free"); }
        if (h->state != kAllocMagic) { ++invalid_free_; throw HeapViolation("invalid free (bad state)"); }
        if (!canaries_intact(h)) { ++overflow_; throw HeapViolation("heap overflow (canary)"); }
        std::memset(payload_of(h), 0xDD, payload_);      // poison freed memory
        h->state = kFreeMagic;
        fl_push(h);                                      // push (your encode_link)
    }

    std::size_t usable_size(const void*) const { return payload_; }
    std::size_t overflow_detections() const { return overflow_; }
    std::size_t double_free_detections() const { return double_free_; }
    std::size_t invalid_free_detections() const { return invalid_free_; }
    bool owns(const void* p) const { return in_range_payload(p); }

    // =====================================================================================
    // YOU IMPLEMENT — the hardening primitives (Lesson 17). Stubs are INSECURE; fix them.
    // =====================================================================================

    // A per-block canary derived from the secret AND the block's address, so every block's canary is
    // different and an attacker can't guess/forge it without `secret_`. STUB: a constant (forgeable).
    std::uint64_t canary_for(const void* block_addr) const {
        (void)block_addr;
        return 0;  // TODO: secret_ ^ (mix of reinterpret_cast<uintptr_t>(block_addr)), e.g. * a prime
    }

    // Write the canary before (header) and after (trailer) the payload, so an overflow in either
    // direction corrupts a canary. Use canary_for(h). STUB already does this — leave as is once
    // canary_for is real (it works for whatever canary_for returns).
    void arm_canaries(Header* h) {
        std::uint64_t c = canary_for(h);
        h->canary = c;
        trailer(h) = c;
    }

    // True iff both canaries still hold their expected value (no overflow corrupted them).
    bool canaries_intact(const Header* h) const {
        std::uint64_t c = canary_for(h);
        return h->canary == c && trailer_const(h) == c;
    }

    // Safe-linking (glibc 2.32): store the "next" pointer XORed with a secret derived from the SLOT's
    // own address, so a leaked/overwritten link is useless without knowing where it lives. Must
    // round-trip: decode_link(encode_link(next, slot), slot) == next. STUB: identity (NO protection).
    std::uintptr_t encode_link(const void* next, const void* slot) const {
        (void)slot;
        return reinterpret_cast<std::uintptr_t>(next);  // TODO: ^ (reinterpret_cast<uintptr_t>(slot) >> 12)
    }
    void* decode_link(std::uintptr_t enc, const void* slot) const {
        (void)slot;
        return reinterpret_cast<void*>(enc);            // TODO: ^ (reinterpret_cast<uintptr_t>(slot) >> 12)
    }

  private:
    static std::size_t align16(std::size_t n) { return (n + 15) & ~std::size_t{15}; }
    Header* header_at(std::size_t i) const { return reinterpret_cast<Header*>(base_ + i * block_); }
    static std::byte* payload_of(Header* h) { return reinterpret_cast<std::byte*>(h) + sizeof(Header); }
    static const std::byte* payload_of(const Header* h) { return reinterpret_cast<const std::byte*>(h) + sizeof(Header); }
    Header* header_from_payload(void* p) const { return reinterpret_cast<Header*>(static_cast<std::byte*>(p) - sizeof(Header)); }
    std::uint64_t& trailer(Header* h) { return *reinterpret_cast<std::uint64_t*>(payload_of(h) + payload_); }
    std::uint64_t trailer_const(const Header* h) const { return *reinterpret_cast<const std::uint64_t*>(payload_of(h) + payload_); }

    void fl_push(Header* h) { h->next_enc = encode_link(free_head_, h); free_head_ = h; }
    Header* fl_decode_next(Header* h) const { return reinterpret_cast<Header*>(decode_link(h->next_enc, h)); }

    bool in_range(const void* h) const {
        auto a = reinterpret_cast<std::uintptr_t>(h), b = reinterpret_cast<std::uintptr_t>(base_);
        return a >= b && a < b + block_ * count_ && (a - b) % block_ == 0;
    }
    bool in_range_payload(const void* p) const {
        auto a = reinterpret_cast<std::uintptr_t>(p), b = reinterpret_cast<std::uintptr_t>(base_);
        return a >= b && a < b + block_ * count_ && (a - b) % block_ == sizeof(Header);
    }

    std::size_t payload_, block_, count_;
    std::byte* base_;
    std::uint64_t secret_ = 0;
    Header* free_head_ = nullptr;
    std::size_t overflow_ = 0, double_free_ = 0, invalid_free_ = 0;

  public:
    std::uint64_t secret() const { return secret_; }     // exposed for a round-trip test
};

}  // namespace mm
