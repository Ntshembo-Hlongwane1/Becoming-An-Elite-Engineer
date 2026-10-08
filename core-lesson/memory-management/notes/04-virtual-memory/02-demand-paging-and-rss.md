# 4.2 — Demand Paging, Page Faults, and RSS vs VSZ

This section is the heart of the lesson and the foundation of your capstone's shadow memory.

## 1. Allocation is lazy

When you `mmap` a region (or `malloc` a big block, which `mmap`s — alignment lesson Part 2 §6), the
kernel does **almost nothing**: it records a VMA (Lesson 3.2) saying "these virtual pages are valid
and anonymous/zero-filled", and returns. **No physical RAM is attached.** The page-table entries are
marked *not present*.

The first time you *touch* a page (read or write), the MMU finds the PTE not present and raises a
**page fault**. The kernel's fault handler then:
1. checks the faulting address is in a valid VMA with the right permissions (else SIGSEGV),
2. allocates a physical **page frame**,
3. for anonymous memory, zero-fills it (security: you never see another process's old bytes),
4. updates the PTE to present, and
5. resumes your instruction as if nothing happened.

This is **demand paging** / lazy allocation. You pay RAM only for pages you actually use.

## 2. Measured: 15 MiB that costs nothing until touched

**(measured on your VM)**:

```
page size = 4096
RSS: before=4040   after mmap(15MiB)=4104   after touching half=12108 kB
resident pages: after mmap = 0 / 4000       after touching half = 2000 / 4000
```

Read it:
- `mmap` of ~15 MiB raised RSS by **64 kB** — essentially nothing — and `mincore` reports **0 of
  4000 pages resident**. The virtual region exists; no physical frames are attached.
- Touching every other page (2000 pages) raised RSS by ~8000 kB = **2000 × 4 KiB**, and `mincore`
  now reports **exactly 2000 resident**. Each touched page cost one frame; the untouched 2000 still
  cost nothing.

That linear "RAM used = pages touched × 4 KiB" is the whole idea, on your machine. **Your capstone
reserves a giant shadow region with `mmap` and relies on exactly this**: the shadow for memory the
target never uses never gets a frame, so a terabyte of sparse shadow costs kilobytes.

## 3. Minor vs major faults

- **Minor fault:** the page can be satisfied without disk I/O — anonymous zero-fill (above), or the
  data is already in the page cache / shared with another process. Fast.
- **Major fault:** the kernel must read from disk (a file-backed page not yet in the page cache, or a
  swapped-out page). Slow (milliseconds). `/proc/self/stat` and `getrusage` count these
  (`ru_minflt`/`ru_majflt`). A program that majors-faults a lot is thrashing.

## 4. VSZ vs RSS (the two "memory" numbers)

- **VSZ (virtual size, `VmSize`)**: the sum of all VMA sizes — every page the process *could* touch.
  Includes the untouched `mmap`, unused `.bss`, reserved stack, etc. Can be enormous and cheap.
- **RSS (resident set size, `VmRSS`)**: the physical RAM actually backing the process right now —
  pages that have been touched and not evicted. This is the number that matters for "am I using too
  much memory".

From the measurement: after the `mmap`, VSZ grew by 15 MiB but RSS by ~0. "My program's VSZ is 2 GB"
is often meaningless; "its RSS is 2 GB" is a problem. `/proc/self/status` exposes both (your exercise
parses it); `mincore()` tells you, page by page, which pages of a mapping are resident.

## 5. `mincore` — asking which pages are resident

```cpp
std::vector<unsigned char> vec(num_pages);
mincore(addr, length, vec.data());   // sets bit 0 of vec[i] if page i is resident
```

`mincore` is how the measurement above counted resident pages, and how your exercise's prober proves
demand paging deterministically: freshly `mmap`'d anonymous pages read back **not resident**; after
you touch page `i`, `vec[i] & 1` becomes set. It's a clean, testable window into the MMU's state.

## Drills
1. Reproduce the §2 experiment. Change "touch every other page" to "touch the first 10 pages" and
   predict the RSS delta and the `mincore` count before running.
2. Touch a page by **reading** it (not writing). Does it become resident? (Anonymous zero pages have
   a subtlety: a read may map a shared zero page COW — observe RSS vs `mincore`.)
3. `mmap` a large **file** instead of anonymous memory; use `getrusage` to count major vs minor
   faults as you read it the first time vs the second time (page cache). Explain.
4. Make a 1-GiB `static char buf[1<<30];` (bss). Does the program's RSS jump at start? Why not?
   Touch one page and recheck.

## My summary
