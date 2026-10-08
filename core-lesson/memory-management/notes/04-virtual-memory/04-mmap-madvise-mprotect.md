# 4.4 — mmap, mprotect, madvise, and the Page Cache

The syscalls that let *you* drive the virtual-memory machinery directly. Your capstone uses all three.

## 1. mmap — create a mapping

`mmap(addr, len, prot, flags, fd, offset)` adds a VMA to your address space. Two axes you must know:

- **backing**: `MAP_ANONYMOUS` (zero-filled memory, no file — heap, big mallocs, thread stacks,
  your shadow region) vs file-backed (`fd` + `offset` — the file's bytes appear as memory; this is
  how programs and libraries are loaded, Lesson 3.2).
- **sharing**: `MAP_SHARED` (writes go to the underlying file / are visible to other mappers) vs
  `MAP_PRIVATE` (copy-on-write; writes stay private — §4.3).

`prot` is `PROT_READ|PROT_WRITE|PROT_EXEC` (or `PROT_NONE`), mapped straight onto the page-table
permission bits (Lesson 3.4). The result is page-aligned (alignment lesson Part 3 §6) and `len` is
rounded up to a page. Release with `munmap(addr, len)`.

File-backed `mmap` *is* the **page cache** (the LSM/alignment lessons' buffered-I/O cache): the pages
you map are the same pages the kernel caches the file in, so `mmap` + read is an alternative to
`read()`, and it's why mixing `mmap` with `O_DIRECT` is incoherent (alignment lesson Part 5 §4).

## 2. mprotect — change permissions

`mprotect(addr, len, prot)` changes an existing mapping's permissions, page-aligned. Uses:
- A **JIT** writes code into a `rw-` page, then `mprotect`s it to `r-x` to run it — never `rwx` at
  once (W^X, Lesson 3.4).
- Your **capstone** does the opposite: `mprotect(PROT_NONE)` on a freed block / guard page, so any
  later access faults (SIGSEGV) and your handler reports a use-after-free or overflow. This turns the
  MMU into a bug detector.

## 3. madvise — hints to the kernel

`madvise(addr, len, advice)` tells the kernel how you'll use a region. The ones that matter here:
- `MADV_DONTNEED`: drop the physical frames now (RSS falls; next touch re-faults zero pages for
  anonymous memory). A way to *release* RAM without unmapping.
- `MADV_WILLNEED`/`MADV_SEQUENTIAL`/`MADV_RANDOM`: prefetch / readahead hints.
- `MADV_DONTFORK`: exclude a region from the child after `fork` — the fix the alignment lesson cited
  for the `O_DIRECT` + fork hazard (Part 5 §10).

## 4. Putting it together (capstone preview)

Your detector will, for a guarded allocation:
1. `mmap` an anonymous region big enough for the block plus a trailing guard page;
2. hand the user a pointer near the end of the usable pages;
3. `mprotect(PROT_NONE)` the guard page so an overflow faults;
4. on free, `mprotect(PROT_NONE)` the whole block (quarantine) so a use-after-free faults;
5. catch SIGSEGV, look up the fault address (Lesson 3's classifier!), and print a report.

Every step is a syscall from this lesson. And the big sparse **shadow** region (the other detection
scheme) is a single `mmap` that stays cheap by §4.2's demand paging.

## Drills
1. `mmap` an anonymous `rw-` page, write to it, then `mprotect(PROT_NONE)` and write again — catch the
   SIGSEGV with a handler and print the fault address (`siginfo_t::si_addr`). This is the capstone's
   core trick in 20 lines.
2. Touch a big anonymous region (RSS up), then `madvise(MADV_DONTNEED)` it and recheck RSS. Where did
   the pages go? What does the next read return?
3. `mmap` a file `MAP_SHARED`, modify a byte through the pointer, `msync`, and confirm the file
   changed on disk. Then do it `MAP_PRIVATE` and confirm the file did *not* change (COW).
4. Explain why `mprotect` lengths and addresses must be page-aligned (Lesson 4.1 §2).

## My summary
