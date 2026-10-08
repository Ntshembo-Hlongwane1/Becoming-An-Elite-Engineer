# Part 7 — The Security Researcher's View

> Alignment code is *arithmetic on sizes and pointers right before memory is handed out or
> written to disk*. That's exactly where attackers look. For each bug class, same three hats as
> always: **Offense: discovery** (how you'd find it black-box or in review), **Offense: value**
> (what it gives an attacker, and the honest *ceiling*), **Defense: impact** (what the business
> loses, and the fix that makes it impossible by construction).
> Companion reading: `[MEM §5.1–5.3]`.

Contents

1. Integer overflow in align-up → undersized allocation (CVE-2013-4332)
2. Freeing the adjusted pointer (bad free)
3. Mismatched allocator / deallocator
4. Padding and tail blocks leak memory to disk (measured: a secret on disk)
5. Misaligned reads in parsers
6. `O_DIRECT` + `fork` → silent corruption
7. Alignment and ASLR: the low bits don't move
8. Checklist for your object store
9. Drills

---

## 1. Integer overflow in align-up → undersized allocation

**The bug (Part 1 §8):** `align_up(x, A) = (x + A - 1) & ~(A - 1)` wraps for x near `SIZE_MAX`
and returns a tiny number. Same for `n * sizeof(T)` in an allocator (Part 3 §9.4). The
allocation succeeds with a few bytes, and the caller, who believes it has `x` bytes, writes far
past the end: a **heap buffer overflow**.

**It happened in glibc.** CVE-2013-4332: "Multiple integer overflows in malloc/malloc.c in the
GNU C Library (aka glibc or libc6) 2.18 and earlier allow context-dependent attackers to cause a
denial of service (heap corruption) via a large value to the pvalloc, valloc, posix_memalign,
memalign, or aligned_alloc functions." `[CVE-2013-4332]`. The fix is one comparison, placed
**before** the arithmetic `[GLIBC-15857-FIX]`:

```c
+  /* Check for overflow.  */
+  if (bytes > SIZE_MAX - alignment - MINSIZE)
+    {
+      __set_errno (ENOMEM);
+      return 0;
+    }
```

Read it line by line: `alignment + MINSIZE` is the most the allocator will add to `bytes`
internally (Part 3 §7: the over-allocation). If `bytes` is bigger than `SIZE_MAX` minus that, the
addition would wrap, so refuse.

- **Offense: discovery.** Look for any `+` on a size that comes before a mask or an allocation.
  The grep is `& ~(` and `+ align - 1`. Black-box: feed boundary sizes (`SIZE_MAX`,
  `SIZE_MAX - 1`, `SIZE_MAX - align + 1`, `2^32 - 1` for 32-bit fields) wherever a length comes
  from input: a length prefix in a file, an HTTP `Content-Length`, a multipart chunk size. Your
  object store will parse lengths from network and disk, so every one is a candidate.
- **Offense: value.** A heap overflow with attacker-controlled length and often controlled
  content. The ceiling is **memory corruption → potentially code execution** (Red Hat: "If an application used such a function, it could cause the application to crash
  or, potentially, execute arbitrary code with the privileges of the user running the
  application." `[RH-CVE-2013-4332]`). Modern heap hardening raises the cost, but the class is
  the real thing.
- **Defense: impact.** Remote crash = availability incident, and possibly a full compromise of the
  storage node (data confidentiality and integrity, the whole business). **By construction:**
  every align-up returns `std::optional` or throws on overflow (Exercise 1), and allocator `n *
  sizeof(T)` is checked (Exercise 4). Lengths parsed from input are bounded by a sane maximum
  *before* any arithmetic.

## 2. Freeing the adjusted pointer (bad free)

**The bug (Part 3 §7b, §8 case 3):** over-allocate, align, then `free(aligned)` instead of
`free(raw)`. glibc's `free` reads the chunk header at `aligned - 16` (Part 2 §5), which is
just bytes inside your own slack region.

**(measured, plain glibc, no sanitizer):**

```
free(): invalid size
Aborted
```

glibc's integrity checks noticed the garbage size and aborted. That's the good outcome.

- **Offense: discovery.** Review heuristic: any pointer that is *moved* (`std::align`, `+=`,
  `& ~mask`) and later passed to a release function. Dynamic: ASan's "attempting free on address
  which was not malloc()-ed".
- **Offense: value.** If the attacker controls the bytes just before the aligned pointer (for
  example, the slack region that held data from an earlier request), they control the "chunk
  header" that `free` reads. Forging a fake chunk header and getting the program to `free` it is
  a known exploitation building block (the "House of Spirit" technique, demonstrated per glibc version in `[HOW2HEAP]`). **Ceiling, honestly:** current glibc validates sizes and alignment in `free`, so
  in practice this is usually a **crash (DoS)**. Getting further needs precise control of the
  forged header *and* a way to defeat those checks, so it's attacker-skill-dependent, not
  automatic.
- **Defense: impact.** A crash on a storage node mid-write can mean a partial file plus recovery
  time (availability), and a crash you can trigger on demand is a DoS lever. **By construction:**
  the raw pointer is *only* ever held by the owning object (RocksDB's `buf_` holds `new_buf`, never
  `bufstart_`, Part 3 §7a). Exercise 2's `AlignedFree` recovers the raw pointer from the header,
  so callers never see it.

## 3. Mismatched allocator / deallocator

`aligned_alloc` → `delete[]`, aligned `new` → unaligned `delete` (Part 3 §8 cases 1–2). Each
allocator family may keep different bookkeeping. Releasing with the wrong one hands the wrong
allocator a pointer it never issued, which is the same class as §2. Discovery: ASan
(`alloc-dealloc-mismatch`, `new-delete-type-mismatch`). Defense by construction: the release
function lives in a *type* (Part 4), and raw `free`/`delete` don't appear in application code
(grep for them in review, and treat each hit as a finding).

## 4. Padding and tail blocks leak memory to disk

**The bug:** an aligned block is padded to 512/4096 bytes for O_DIRECT (Part 5 §7), and the
padding isn't zeroed. `aligned_alloc` memory "is not zeroed" `[MAN-aligned_alloc]`, so the
padding contains whatever the allocator recycled.

**(measured)**: one part of a program puts a secret in a buffer and frees it. The storage code
then allocates a block of the same size, writes 12 real bytes, and sends a full 512-byte block
with `O_DIRECT`:

```
$ strings -n 6 leak_probe.bin
hello, 13 B
API_KEY=sk_live_DEADBEEF

00000000: 6865 6c6c 6f2c 2031 3320 420a 0000 0000  hello, 13 B.....
00000060: 0000 0000 4150 495f 4b45 593d 736b 5f6c  ....API_KEY=sk_l
00000070: 6976 655f 4445 4144 4245 4546 0000 0000  ive_DEADBEEF....
```

The allocator handed back the same block, and the "padding" carried the secret onto disk. Even
`ftruncate` afterwards wouldn't help for sure: truncation changes the file *length*, but the
bytes were already written to the device sector, so they're **data remanence** **(derived;
`[LSM-P1]`/`[LSM-P2 §3]` showed the struct-padding version of this)**.

- **Offense: discovery.** `strings`/`xxd` on files the system produces: the slack after the
  logical end, the padding inside records, the unused tail of blocks. On a raw device or a disk
  image, look *past* the file length too. In your future object store, any "block-aligned" object
  format is a place to look.
- **Offense: value.** **Information disclosure**: keys, tokens, other tenants' object data in a
  multi-tenant store. The ceiling is whatever was in that heap memory, and the attacker needs read
  access to the stored file or the device. Often that's a lower-privileged backup or replica
  reader, which is a realistic position to be in.
- **Defense: impact.** Cross-tenant leak = confidentiality breach, possibly with regulatory
  consequences (any personal data in the leaked bytes). **By construction:** `AlignedBuffer` zero-fills
  on allocation (Exercise 3 requirement), and `DirectAppender` pads with explicit zeros (Exercise 5,
  as RocksDB does with `PadToAlignmentWith(0)` `[ROCKS-SRC:writable_file_writer.cc]`).

## 5. Misaligned reads in parsers

**The bug (Part 1 §12):** `*reinterpret_cast<const uint32_t*>(buf + offset)` where `offset` comes
from the file. That's UB whenever `offset % 4 != 0`.

- **Offense: discovery.** grep `reinterpret_cast<const uint` in parsing code. UBSan
  ("load of misaligned address") under a fuzzer that mutates offsets.
- **Offense: value.** On x86 it usually "works" (Part 1 §12 printed `7`). On strict-alignment
  CPUs, or when the compiler emits vector instructions that assume alignment, it can fault (a crash).
  **Ceiling:** DoS. The bigger risk is that the same code usually has no **bounds** check either,
  and that one is an out-of-bounds read.
- **Defense: impact.** A parser crash on one crafted object = a poisoned object that crashes
  every node that reads it. **By construction:** decode with `std::memcpy` or shift-based
  `DecodeFixed32` (the way your LSM Exercise 1 M1 already requires) and bounds-check before
  decoding.

## 6. `O_DIRECT` + `fork` → silent corruption

Part 5 §10: direct I/O into private (heap/static) memory concurrent with `fork` "can result in
data corruption and undefined behavior in parent and child processes" `[MAN-open]`.

- **Offense: discovery.** The precondition is architectural: a server that both forks
  (worker model, or `system()`/`popen()` for a helper tool) and does direct I/O from threads.
  Find it by reading the process model, not the I/O code.
- **Offense: value.** Hard to *control*, so it's mostly an integrity/availability bug, not a
  precise primitive. **Ceiling:** corrupt or mixed data written to disk. Honest assessment: rarely
  exploitable on purpose, but very expensive when it happens.
- **Defense: impact.** Silent corruption in a storage system is the worst failure mode: backups
  faithfully copy the corrupt data. **By construction:** no `fork` in the storage process (spawn
  helpers via `posix_spawn` from a separate small process), or I/O buffers from
  `mmap(MAP_SHARED)` / `madvise(MADV_DONTFORK)` `[MAN-madvise]`, and end-to-end checksums so
  corruption is at least *detected*.

## 7. Alignment and ASLR: the low bits don't move

**(measured)** four runs of a program printing a global, a heap and a stack address:

```
global=0x60331d896014 heap=0x603337de1010 stack=0x7fff4842637c
global=0x592565c33014 heap=0x5925700d4010 stack=0x7ffdae392d4c
global=0x63fd5384b014 heap=0x63fd8a8cd010 stack=0x7fff00ecd83c
global=0x58ccd64f5014 heap=0x58ccf6a37010 stack=0x7ffce22af66c
```

The global always ends in `014` and the first heap block always ends in `010`. ASLR moves memory
regions by **whole pages**, and the low 12 bits of an address are the offset *inside* a page,
which the mapping doesn't change (Part 1 §3) **(derived)**. (The stack also gets sub-page
randomisation here.)

- **Offense: value.** This is why **partial pointer overwrites** work: overwriting only the low
  one or two bytes of a pointer redirects it within a known neighbourhood without needing to
  defeat ASLR **(derived)**. For you as a future reverse engineer, the low 3 hex digits of a
  leaked address identify *which* object or function it points to, even across runs.
- **Defense.** Alignment and page granularity are physics you can't remove. Defend at the
  overflow (§1–§4) so attackers never get a partial overwrite in the first place.

## 8. Checklist for your object store

When you build the storage layer, check each item. Then, in the red-team phase, come back to
this list *black-box*:

- [ ] Every size arithmetic before allocation or I/O is overflow-checked (§1).
- [ ] No raw `free`/`delete`/`munmap` outside an owning type (§2, §3).
- [ ] Every padded block is zero-filled before it's written (§4).
- [ ] No pointer-cast decoding of on-disk/on-wire integers (§5).
- [ ] No `fork` in the process that does direct I/O, or buffers are `MADV_DONTFORK` (§6).
- [ ] Tests run on the real filesystem, not tmpfs (Part 5 §6).
- [ ] Sanitizers (ASan + UBSan) are on in every test build.

## 9. Drills

1. Write `align_up` *without* the overflow check, call it with `SIZE_MAX - 10` and 4096, pass the
   result to `aligned_alloc`, then `memset` the "requested" size. Run under ASan. Then add the
   check.
2. Re-run the §4 leak demo, then fix it in *two* different ways (zero on allocation; zero only
   the padding). Measure the cost of each with 1 GiB of writes.
3. Search the RocksDB source for `PadToAlignmentWith`. Is every padding path zero-filled?
   (This is real source review. Write down what you found either way.)
4. Fuzz drill: write a 30-line parser that reads `uint32_t` length + bytes from a file using
   `reinterpret_cast`. Build with UBSan, feed it 1000 random files, collect the reports. Rewrite
   with `memcpy` + bounds checks, then repeat.
5. Explain to a non-engineer (3 sentences, business language) why §4 matters for a company storing
   customer files.

## My summary

