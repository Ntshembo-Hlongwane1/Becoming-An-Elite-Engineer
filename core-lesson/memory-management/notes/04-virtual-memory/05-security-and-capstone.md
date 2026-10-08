# 4.5 — Security & Capstone View: The MMU as a Bug Detector

Virtual memory is both an attack surface and the mechanism behind the best detection tools —
including the one you'll build. Three hats, plus the explicit capstone bridge.

## The MMU as defender (your capstone's core)
The page table checks permissions on *every* access in hardware, for free. You can co-opt that:
- **Guard pages** (`PROT_NONE`): place one after an allocation; an overflow writes into it and faults
  instantly, with the exact faulting address (`si_addr`). No red-zone scanning needed — the MMU does
  it. Cost: one page + one VMA per guarded allocation (memory-heavy, which is the tradeoff vs shadow).
- **Quarantine by protection**: on free, `mprotect(PROT_NONE)` the block; a use-after-free faults
  instead of silently reading recycled bytes (Lesson 2.5). This is real ASan's and Electric Fence's
  strategy, and yours.
- **Shadow memory**: a second, compact map of "is this byte poisoned?" The reason it's affordable is
  §4.2 demand paging — you `mmap` a vast shadow region and only the parts covering touched memory
  ever get frames. **This lesson is why the capstone's shadow approach is viable at all.**

## Offense — what VM gives an attacker
- **Discovery:** reading `/proc/<pid>/maps` (Lesson 3) reveals the live layout; `/proc/<pid>/pagemap`
  can even expose virtual→physical mappings (now restricted precisely because it helped attacks).
- **Value / ceiling:** VM-level primitives are usually *enablers*, not payloads. Knowing physical
  addresses or page layout helps defeat ASLR or mount hardware attacks (e.g. Rowhammer flips bits in
  physical DRAM rows — `[HPC §9.4]` references a simulated Rowhammer secure-boot bypass). Ceiling
  depends entirely on what the leak/flip is combined with.
- **COW / overcommit pitfalls:** a write to a COW page is a hidden allocation and a hidden latency
  spike; under memory pressure it can trigger OOM. Not usually an exploit, but a reliability/DoS
  concern for a service (availability = your project's uptime).

## Defense — construction
- Let unmapped gaps and guard pages catch wild accesses (Lesson 3.4) — don't fill every gap.
- Pre-touch or `mlock` memory whose allocation must not fail late under overcommit (§4.3).
- `MADV_DONTFORK` (or no `fork`) for DMA/`O_DIRECT` buffers (alignment lesson Part 5 §10).
- In tests/CI, run under ASan — whose guard pages, quarantine and shadow are exactly this lesson,
  which Lesson 16 dissects and your capstone rebuilds.

## Through-line
Lesson 3 gave you the map; Lesson 4 gave you the machine that enforces it and the two detection
primitives (guard pages via `mprotect`, cheap shadow via demand paging). The exercise builds the
measurement tools (`mincore`, `/proc/status`) your capstone needs to *prove* it works.

## My summary
