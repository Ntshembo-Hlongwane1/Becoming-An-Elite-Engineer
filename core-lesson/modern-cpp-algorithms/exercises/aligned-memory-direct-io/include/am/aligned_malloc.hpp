#pragma once
// Exercise 2 (beginner+) — Build aligned allocation yourself, on top of plain malloc.
// Notes: Part 3 §7 (over-allocate + round up, the header trick), Part 3 §8, Part 7 §1–§2.
//
// This is how you'd provide aligned_alloc on a platform that lacks it, and it's the classic
// interview question. Requirements:
//
//  * ONLY std::malloc / std::free underneath. Not aligned_alloc, posix_memalign, aligned new, mmap.
//  * Layout (Part 3 §7c):  [ slack ][ void* raw ][ user bytes ... ]
//                                                ^ returned pointer, aligned to `align`
//    AlignedFree(p) reads the raw pointer stored just before p and frees THAT.
//    Store/load the raw pointer with std::memcpy (the header slot is not guaranteed to be suitably
//    aligned for a void* when align < alignof(void*) — Part 1 §12). Or prove it always is, and
//    write the proof in DECISIONS.md.
//  * `align` must be a power of two (any, including 1, 2, 4). Otherwise: return nullptr, errno=EINVAL.
//  * Overflow: if size + (bookkeeping) does not fit in size_t, return nullptr with errno=ENOMEM
//    WITHOUT calling malloc (this is exactly glibc's CVE-2013-4332 fix — Part 7 §1).
//  * size == 0 is valid: return a unique, non-null, aligned pointer that AlignedFree accepts.
//  * AlignedFree(nullptr) is a no-op.
//  * Use your Exercise 1 functions (AlignUp / PaddingTo / IsPowerOfTwo).
//
// Write in DECISIONS.md: how many extra bytes does your scheme request for (size, align)? What is
// the worst case for align = 4096? Compare with the 8192-byte spacing measured in Part 3 §10.
#include <cstddef>

namespace am {

void* AlignedMalloc(std::size_t size, std::size_t align);
void AlignedFree(void* p);

}  // namespace am
