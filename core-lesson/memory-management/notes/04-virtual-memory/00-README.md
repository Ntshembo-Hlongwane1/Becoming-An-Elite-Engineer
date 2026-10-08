# Lesson 4 — Virtual Memory: Pages, Faults, and Copy-on-Write

> Status: complete. Exercise: `../../exercises/04-virtual-memory/`.
>
> Lesson 3 showed the address-space *map*. This lesson is the machinery that makes the map real: how
> a virtual address becomes a physical one, a **page** at a time, only when you touch it. This is the
> most capstone-critical lesson in Phase A: your detector reserves a *huge* shadow region and relies
> on the fact that unused pages cost nothing until touched — which is this lesson, measured.

Read in order:
1. `01-virtual-vs-physical.md` — virtual addresses, pages & frames, page tables, the MMU, the TLB.
2. `02-demand-paging-and-rss.md` — lazy allocation, page faults (minor/major), VSZ vs RSS, `mincore`,
   the measured "mmap 15 MiB costs ~0 until touched" experiment.
3. `03-cow-and-overcommit.md` — copy-on-write, `fork`, overcommit, the OOM killer.
4. `04-mmap-madvise-mprotect.md` — `mmap` (file/anon, shared/private), `mprotect`, `madvise`; tie to
   the page cache and the alignment lesson's `O_DIRECT`.
5. `05-security-and-capstone.md` — guard pages, `PROT_NONE`, why shadow memory is cheap, W^X; three hats.
6. `06-glossary.md`.

Then do the exercise: **page arithmetic + a `/proc/self/status` parser + a `mincore` demand-paging
prober** — the exact primitives your capstone's shadow memory is built on.

## The one-paragraph picture
Your pointers are **virtual** addresses. The CPU's **MMU** translates each one to a **physical**
RAM address using per-process **page tables**, in fixed **page** units (4096 bytes here), caching
recent translations in the **TLB**. When you `mmap` memory, the kernel only records the mapping; no
physical **page frame** is attached until you first touch a page, which triggers a **page fault**
that the kernel services by allocating and wiring up a frame (**demand paging** / lazy allocation).
`fork` doesn't copy memory either — parent and child share frames marked **copy-on-write**, and only
a page that one side *writes* gets duplicated. This is why a program's **VSZ** (virtual size) can be
huge while its **RSS** (resident, real RAM) is tiny, and why your capstone can reserve a terabyte of
sparse shadow space for almost nothing.
