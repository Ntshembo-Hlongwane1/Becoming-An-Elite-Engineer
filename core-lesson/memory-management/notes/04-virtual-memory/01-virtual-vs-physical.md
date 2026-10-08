# 4.1 — Virtual vs Physical, Pages, and the MMU

## 1. Two kinds of address

Every address your program has ever printed (Lessons 1–3) is a **virtual address** — a number
meaningful only inside *this* process. The RAM chips use **physical addresses**. Between them sits
hardware, the **MMU** (Memory Management Unit), translating virtual → physical on *every* memory
access `[KERNEL-MM]`.

Why the indirection exists `[KERNEL-MM]` (and OSTEP's VM chapters):
- **Isolation:** process A's address `0x1000` and process B's `0x1000` map to *different* physical
  RAM, so they can't see each other. (This is what makes the address space "private" — Lesson 3.)
- **Flexibility:** a contiguous-looking virtual region can be backed by scattered physical frames, or
  by none yet (demand paging, §4.2), or shared between processes (COW, §4.3).
- **Protection:** permissions (r/w/x, Lesson 3.4) are enforced per translation.

## 2. Pages and frames

Translation isn't per byte — that would need a table the size of memory. It's per **page**: a
fixed-size, aligned block of the virtual space. The physical block it maps to is a **page frame**.
On your VM the page size is **4096 bytes (measured, `sysconf(_SC_PAGESIZE)`)** — the same 4096 that
has shown up as the VM page in the alignment lesson and the filesystem block in Lesson… /the LSM
lesson. Not a coincidence: the kernel aligns these to the MMU page.

Because pages are 4096 = 2¹² bytes, any address splits cleanly (Lesson 1 §6) into:

```
 63                12 11            0
+--------------------+--------------+
|   page number      | offset (12)  |
+--------------------+--------------+
```

- The low 12 bits are the **offset within the page** — translation never changes them.
- The high bits are the **virtual page number**, which the page table maps to a physical **frame
  number**.

This is why, in the alignment lesson (Part 7 §7), ASLR could only randomise the high bits: it moves
whole pages, leaving the low 12 bits of an address fixed. Same fact, seen from the hardware now.

## 3. Page tables

The map from virtual page → physical frame (plus permission bits) is the **page table**, one per
process, maintained by the kernel and walked by the MMU. On x86-64 it's a 4- or 5-level tree
(a radix trie keyed by chunks of the page number); the details are Lesson-7-adjacent, but the shape
you must know is: **each process has its own page tables, so each has its own private map, and a
context switch swaps which page-table tree is active.**

A page-table entry (PTE) holds the frame number and bits: present?, writable?, user/kernel,
executable (NX, Lesson 3.4), accessed, dirty. "This VMA is `rw-p`" (Lesson 3.2) ultimately means
"its PTEs have write set, execute clear".

## 4. The TLB

Walking a 4-level tree on every access would be slow, so the MMU caches recent virtual→physical
translations in the **TLB** (Translation Lookaside Buffer), a small associative cache. A **TLB hit**
translates in ~1 cycle; a **TLB miss** triggers a page-table walk. Consequences that matter for
performance work (Lesson 6):
- Touching memory with good locality reuses TLB entries (fast); jumping across many pages thrashes
  the TLB (slow) even if the data is in cache.
- **Huge pages** (2 MiB) exist partly to cover more memory per TLB entry.
- A context switch may flush the TLB, which is part of why switching processes is expensive.

## 5. What this means for a pointer

When you dereference `int* p`:
1. The CPU splits `p` into page number + offset.
2. It looks up the page number in the TLB; on a miss, the MMU walks the page table.
3. If the PTE says **not present**, the CPU raises a **page fault** (§4.2) — the kernel steps in.
4. If present but the access violates permissions (write to `r--`, execute `rw-`), it's a
   **protection fault** → SIGSEGV (Lesson 3.4; your capstone's guard pages weaponise this).
5. Otherwise it forms the physical address (frame + offset) and completes the access.

Every single memory access does this. The miracle is that it's usually ~1 cycle because of the TLB
and because the common case is "present, permitted".

## Drills
1. Print `sysconf(_SC_PAGESIZE)`. Then take any pointer, mask off the low 12 bits (`p & ~0xFFF`) to
   get its page base and `p & 0xFFF` for its offset (Lesson 1 §7–8). Verify two addresses in the same
   4096-block share a page base.
2. Explain, using §2, why ASLR can't randomise the low 12 bits of an address.
3. Read `/proc/self/status` for `VmSize` (virtual) and `VmRSS` (physical). Why is the first bigger?
   (Answer is §4.2, but predict now.)
4. Why does a process's `0x1000` not collide with another process's `0x1000`? Which per-process
   structure guarantees that?

## My summary
