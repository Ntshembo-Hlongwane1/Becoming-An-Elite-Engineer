# 4.3 — Copy-on-Write, fork, and Overcommit

## 1. fork doesn't copy memory

`fork()` creates a child process that is a near-duplicate of the parent. Copying the parent's entire
address space would be slow and usually wasteful (the child often immediately `exec`s). So the kernel
**shares** every page between parent and child, marking them **copy-on-write (COW)**: the PTEs are
set read-only, and both processes point at the *same* physical frames.

As long as nobody writes, they share RAM for free. The instant either side **writes** a shared page,
the MMU raises a protection fault; the kernel copies that one page into a new frame, makes the
writer's PTE point at the copy (writable), and resumes. Only written pages are duplicated, one page
at a time, on demand.

## 2. Measured: shared vs private across fork

**(measured on your VM)**: parent maps one `MAP_SHARED` page and one `MAP_PRIVATE` page, sets both to
10, forks; the child writes 99 to both and exits; parent reads back:

```
after child writes 99: shared(MAP_SHARED)=99   priv(MAP_PRIVATE,COW)=10
```

- `MAP_SHARED`: there is genuinely one frame; the child's write is visible to the parent (99). This is
  how shared-memory IPC works.
- `MAP_PRIVATE`: COW. The child's write triggered a private copy; the parent's page is untouched (10).
  Ordinary memory (heap, stack, globals) is private, so a child can scribble freely without
  corrupting the parent.

This is also the exact hazard the alignment lesson warned about (Part 5 §10): doing `O_DIRECT` DMA
into a private page while `fork` makes it COW can leave the DMA writing to the wrong copy. Same COW
mechanism, seen now from the memory side.

## 3. Overcommit

Because allocation is lazy (§4.2), the kernel lets you `mmap`/`malloc` **more memory than exists** —
it's betting you won't touch all of it. This is **overcommit**. `malloc(100 GB)` on a 16 GB machine
can *succeed* (you got virtual address space + VMAs), and only start consuming RAM as you touch pages.

Controlled by `/proc/sys/vm/overcommit_memory` (0 heuristic, 1 always, 2 strict/accounted). The
consequence of the common heuristic mode: a `malloc` that "succeeded" can still fail *later*, at
**touch** time, when there's no frame to give you.

## 4. The OOM killer

If every process touches its promised pages and physical RAM + swap runs out, the kernel can't
conjure frames. It invokes the **OOM (out-of-memory) killer**, which picks a process (by an
`oom_score`) and kills it to reclaim memory. So on Linux, "out of memory" often shows up not as a
failed `malloc` returning null, but as your process being *killed* mid-run (`dmesg` shows "Out of
memory: Killed process …").

Implications for a systems engineer:
- Checking `malloc != nullptr` is necessary but **not sufficient** under overcommit — the failure can
  come later at first touch.
- Pre-touching (or `mlock`-ing) critical memory up front turns a late, unpredictable OOM into an
  early, handleable one — relevant to real-time and storage systems (and your project).

## Drills
1. Reproduce §2 with `MAP_SHARED` vs `MAP_PRIVATE`. Then add a grandchild and confirm each private
   write is isolated.
2. `malloc` a block far larger than your RAM; check it "succeeds". Then touch it page by page while
   watching RSS (`watch cat /proc/self/status` or print in a loop) until the OOM killer fires (do
   this in a VM/cgroup you don't mind killing). Find the kill message in `dmesg`.
3. Explain why a server that forks worker processes benefits from COW at startup, and what happens to
   that benefit if each worker immediately writes all its pages.
4. Connect COW here to the alignment lesson's `O_DIRECT` + fork corruption warning.

## My summary
