# Aligned Memory & Direct I/O — A Build-Up Lesson From Zero (Systems-Engineer Edition)

> Goal: by the end you can explain, **down to the last bit of the address**, why
> `write()` on an `O_DIRECT` file descriptor returns `EINVAL`, how every way of getting aligned
> memory in C++ works internally, which deallocator matches which allocator, how production
> storage engines (RocksDB, PostgreSQL) handle the problem, and what an attacker sees in this code.

## Where this lesson came from

Your drill `notes/drills/drill1.cpp` did this:

```cpp
std::vector<char> buffer(4096, 'x');
FileManager fm = FileManager{ path };   // open(..., O_RDWR | O_CREAT | O_DIRECT, 0644)
fm.Write(buffer);                       // -> "Write to: (path.txt) failedInvalid argument"
```

The short answer is "the buffer isn't aligned". This lesson is the long answer: **what** an
alignment is, **who** decides where your bytes land in memory, **why** the kernel and the disk
care, **how** to get alignment correctly and release it correctly, and **what goes wrong** —
including in your two books.

## Corrections to what I told you in chat (read this first)

Writing this lesson meant *measuring* instead of recalling, and two things I said in chat
turned out to be wrong for your machine. That is itself the lesson: **measure, don't assume**.

| What I said in chat | What is actually true on your VM **(measured, Part 5 §6)** |
|---|---|
| "O_DIRECT needs 4096-byte alignment" | Your ext4 disk reports `stx_dio_mem_align=512 stx_dio_offset_align=512`. A buffer at an offset of 512 into a page **works**; 4096 is a *safe superset*, not the rule. |
| "On tmpfs `open()` fails with `O_DIRECT` (EINVAL)" | On your kernel (7.0) tmpfs **accepts** `O_DIRECT` and ignores alignment entirely — every misaligned write succeeded. So if you test in `/tmp` your bug *disappears*. |

## Ground rules (same as the LSM-tree lesson)

1. **Zero assumed knowledge of concepts.** You can write C++; nothing else is assumed. Bits,
   hex, addresses, `uintptr_t`, masks, alignment, padding, allocators, page cache, DMA — all
   taught before use.
2. **Every claim is sourced.** Tags like `[MAN-open]` point to `SOURCES.md`. Things I work out
   are marked **(derived)**; things I ran on your VM are marked **(measured)** with the code.
3. **Your two books are the spine.** `[MEM §x]` = *Advanced Memory Management in Modern C++*
   (2nd ed.), `[ALGO §x]` = *Modern C++ Algorithms*. When a book example is wrong I show the
   compiler/sanitizer output that proves it. Five `[MEM]` examples in this area do not compile, or
   compile but are unsafe (Part 3 §9).
4. **Every code example is explained line by line.** If a line has no explanation, that's a bug
   in the notes.

## Reading order

| # | File | What you learn | Book sections |
|---|---|---|---|
| 1 | `01-bits-addresses-alignment.md` | bits, hex, addresses, `uintptr_t`, powers of two, the `& (N-1)` mask, align-up/down, `alignof`/`alignas`, padding, why hardware cares, UB | `[MEM §2.2, §3.1]`, `[ALGO §22.1]` |
| 2 | `02-where-memory-comes-from.md` | stack / static / heap, what `malloc` and `new` promise (16 bytes) and *why*, glibc chunk headers, the mmap threshold, why your vector landed at +32 | `[MEM §1.2, §14.6]` |
| 3 | `03-getting-aligned-memory.md` | all 7 ways to get aligned memory, line by line; **matching deallocators**; `std::align`; over-allocate-and-mask; costs; book errata | `[MEM §14.5, §14.6, §17.2, §17.3, §17.5, §17.6]` |
| 4 | `04-owning-aligned-memory.md` | RAII: `unique_ptr` + deleters, an `AlignedBuffer` class, `std::span`, a correct `AlignedAllocator<T, N>` for `std::vector`, `std::pmr` | `[MEM §4.1, §4.2, §17.2, §17.6]` |
| 5 | `05-the-io-path-and-o-direct.md` | page cache vs direct I/O, DMA, the three alignment rules, `statx(STATX_DIOALIGN)`, measured EINVAL matrix, unaligned tails, measured performance, durability (O_DIRECT ≠ fsync), `fork` hazard, how RocksDB & PostgreSQL do it | `[MEM §18.6]`, `[ALGO §11.1]` |
| 6 | `06-your-drill-diagnosed.md` | every line of your `FileManager` + `drill1.cpp` reviewed against Parts 1–5 (diagnosis only — the fix is Exercise 6) | — |
| 7 | `07-security-researcher-view.md` | three hats: integer overflow in align-up (CVE-2013-4332), bad-free of adjusted pointers, padding/tail data leaks, misaligned-access DoS, O_DIRECT + fork corruption | `[MEM §5.1]` |
| 8 | `08-glossary.md` | every term, one line each | — |

Then: `../../exercises/aligned-memory-direct-io/` — **six exercises, beginner → advanced**:

| Ex | Level | You build | Do it after |
|---|---|---|---|
| 1 | beginner | alignment arithmetic (`IsPowerOfTwo`, `AlignUp` with overflow check, …) | Part 1 |
| 2 | beginner+ | `AlignedMalloc/AlignedFree` from plain `malloc` (over-allocate + header) | Part 3 |
| 3 | intermediate | `AlignedBuffer` — move-only RAII owner | Part 4 §1–3 |
| 4 | intermediate+ | `AlignedAllocator<T, N>` that works with `std::vector` *and* `std::list` | Part 4 §4–5 |
| 5 | advanced | `DirectFile` + `DirectAppender` — O_DIRECT I/O with `statx`, validation, partial writes, unaligned tails | Part 5 |
| 6 | advanced | fix your drill with Ex 3+5, then **measure** buffered vs direct and write up `RESULTS.md` | Part 6 |

## How to study each part

1. Read once straight through for the story.
2. Re-read with the sources open (links in `SOURCES.md`). Re-run at least one **(measured)**
   experiment per part yourself — your numbers will differ slightly; explain why.
3. Do the **Drills** at the end of each part (5–20 min each).
4. Write a 5-line summary in your own words under `## My summary` at the bottom of each file.

## The one-paragraph picture (you will understand every word of this by Part 5)

Memory is a giant array of bytes and an address is an index into it. An address is
**N-aligned** when it is a multiple of N, which for a power-of-two N means its low log₂N bits are
zero. `malloc`/`new` only promise 16-byte alignment on x86-64 Linux (enough for any ordinary C++
type), so a `std::vector<char>` lands wherever the allocator's bookkeeping puts it — on your run,
32 bytes past a 512 boundary. Normal `write()` doesn't care, because the kernel *copies* your
bytes into its page cache. `O_DIRECT` skips that copy: the storage device reads your memory directly
via **DMA**, in whole **logical blocks**, so the kernel demands that the buffer address, the
length, and the file offset are all multiples of the device's direct-I/O alignment (512 on your
disk, queried with `statx(STATX_DIOALIGN)`) and returns `EINVAL` otherwise. You get aligned
memory from `std::aligned_alloc`/`posix_memalign` (release with `free`),
`operator new(n, std::align_val_t)` (release with the *matching* aligned `operator delete`),
`alignas` storage, `mmap`, or by over-allocating and rounding up — and you wrap whichever you choose
in RAII so it is released exactly once by the right function.
