# Lesson 17 — The Attacker's View of Memory, and Hardening (Phase E)

> Status: complete. Exercise: `../../exercises/17-hardened-allocator/`.
>
> This is the capstone of the curriculum and of the security thread that ran through every lesson. You
> have built an allocator (Lesson 7), seen exactly how its metadata is corrupted into arbitrary writes
> (Lesson 7.5, 8.5, 13.5, 15.5), and built the detector that catches abuse of it (Lesson 16). Now you
> close the loop: **take the attacker's view of the whole heap, then harden an allocator so the
> attacks become impossible or at least expensive.** The defences — **canaries**, **safe-linking**,
> **guard pages**, poison-on-free, double-free detection, isolation — are exactly what glibc, Chrome's
> PartitionAlloc, and hardened_malloc ship, and you'll add them to your own allocator. The through-line
> of the whole course lands here: *build it, break it, defend it* — the complete loop that defines the
> reverse-engineer / 0-day-researcher you're training to be.

This is Phase E, Lesson 2 (tools & security) — the final lesson. It synthesises Lessons 7 (allocator),
7.5/8.5 (heap attacks), 3.4/4.4 (NX/ASLR/`mprotect` guard pages), 5.5 (canaries), 10.5 (custom-allocator
blindness), and 16 (detection).

## Ground rules recap
Every claim is quoted or marked **(derived)** / **(measured)** (run on your VM — GCC 15.2.0). New
sources at the bottom and in `SOURCES.md`; many are reused from earlier lessons.

## Read in order
1. `01-the-attackers-model.md` — the heap as a weapon: overflow→metadata corruption, UAF, double-free,
   free-list pointer injection (fastbin/tcache poisoning, unlink), ABA; the corrupt→groom→write→control
   pipeline; and the honest ceiling (what still gates escalation).
2. `02-canaries-and-poisoning.md` — per-block **canaries** (random, secret-derived) to catch overflow
   that ASan can't see inside an arena; poison-on-free; a **double-free magic** (glibc tcache key).
   Measured: your allocator's canary catches an intra-arena overflow ASan is blind to.
3. `03-safe-linking-and-metadata-protection.md` — glibc **safe-linking** (2.32): XOR free-list pointers
   with a per-run secret; alignment/size sanity checks; out-of-line metadata — defeating the
   pointer-injection attacks of Lesson 7.5.
4. `04-guard-pages-and-isolation.md` — **guard pages** (`PROT_NONE`, Lesson 4.4) turn overflow into a
   hardware fault; NX/ASLR (Lesson 3.4); **partitioned / type-isolated heaps** (PartitionAlloc,
   hardened_malloc, kalloc_type); quarantine. Measured: a guard page SIGSEGVs the overflow.
5. `05-security-view.md` — the synthesis: build→break→defend, what's impossible-by-construction vs
   best-effort, the arms race, and the bridge to your capstone and RE/0-day work.
6. `06-glossary.md`.

Then do the exercise: **harden a free-list allocator** — implement per-block secret canaries and
safe-linking of the free list on a provided allocator, so it detects heap overflow, double-free,
invalid-free, and corrupted-link injection. Tested under ASan+UBSan (the point being that ASan can't
see the intra-arena corruption — your hardening must).

## The one-paragraph picture
An attacker treats the heap as a programmable surface: a buffer **overflow** writes past your data into
the allocator's **metadata** (Lesson 7.2) — a chunk size, a free-list pointer — and a **use-after-free**
or **double-free** puts a freed chunk back in play; with careful **heap grooming** the attacker turns
"corrupt one pointer" into "make `malloc` return an address I chose" → an **arbitrary write** → control
of the program (Lesson 7.5). Hardening raises the cost of each step. **Canaries** — random, per-block,
secret-derived values placed around your payload — make an overflow that reaches metadata *detectable*
(the canary changed), which is critical because when the allocator carves one big region (Lesson 10.5)
**AddressSanitizer can't see the intra-arena overflow** — the allocator must check itself. **Safe-linking**
stores free-list pointers XORed with a per-run secret derived from their own location, so a leaked or
overwritten link is useless without that secret — defeating fastbin/tcache pointer injection. A **guard
page** (`PROT_NONE`) past a region turns a linear overflow into an immediate hardware fault instead of
silent corruption. **Poison-on-free** and a **double-free magic** catch stale reuse. And **isolation** —
putting attacker-controlled data in its own partition away from pointers — removes whole bug classes by
construction. None of these makes exploitation *impossible* in general; together they turn "one bug →
RCE" into "needs several bugs, an info leak, and luck" — which is what real-world memory safety is.

## New / reused sources (also in `SOURCES.md`)
| Key | Source |
|---|---|
| `[SAFELINK]` | glibc 2.32 Safe-Linking of singly-linked free lists (fastbins/tcache). https://sourceware.org/git/?p=glibc.git;a=commit;h=a1a486d70ebcc47a686ff5846875eacad0940e41 |
| `[MAN-mprotect]` | `mprotect(2)` — `PROT_NONE` guard pages (reused from Lesson 4.4). https://man7.org/linux/man-pages/man2/mprotect.2.html |
| `[HARDENED-MALLOC]` | D. Micay, *hardened_malloc* (GrapheneOS) — out-of-line metadata, guard pages, randomization, isolation. https://github.com/GrapheneOS/hardened_malloc |
| `[PARTITIONALLOC]` | Chromium *PartitionAlloc* — partitioned/type-isolated heap. https://chromium.googlesource.com/chromium/src/+/HEAD/base/allocator/partition_allocator/PartitionAlloc.md |
| `[MEM §1.x, §13]` | Alheraki memory book — heap overflows; AddressSanitizer / hardening tooling. |
| `[HPC §4]` | Alheraki, *The Hidden Power: C++26 in Offensive & Defensive Cybersecurity* — offensive/defensive heap techniques. |
