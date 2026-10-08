# 16.3 — Leak detection and reachability

A leak detector seems trivial — "track allocations; whatever's never freed leaked." The subtlety is
*when* you decide, and what "leaked" means. This is what separates a naïve "still-live at exit"
counter from LeakSanitizer.

## 1. The naïve detector (and your exercise)

Keep a side table of live blocks (§16.1): **insert on alloc, erase on free**. Query it any time: the
entries still present are the blocks currently outstanding. At a chosen point you report them.
`[MEM §13.1]` describes LeakSanitizer this way — "finding memory leaks by tracking memory allocation
and deallocation" — and shows the report:

```
==1234==ERROR: LeakSanitizer: detected memory leaks
==1234==LEAK SUMMARY:  definitely lost: 10 bytes in 1 blocks
```

This is exactly what your exercise builds: a table, `track`/`untrack`, and a report of what's still
live. For a *bounded, deterministic test* it's perfect — you allocate, free some, and ask "what's
outstanding?"

## 2. Why "still live at exit" over-reports: reachability

Run the naïve detector program-wide and report *everything* outstanding at program exit, and you get a
flood of **false positives**: the C++ runtime, iostreams, locale data, and countless singletons
allocate once and intentionally never free (freed implicitly by the OS at exit). Those aren't bugs —
they're live-for-the-program's-lifetime. A useful tool must distinguish:

- **definitely lost / leaked** — no pointer to the block exists anywhere, so no code *could* ever free
  it. A real leak.
- **still reachable** — the block is never freed, but a pointer to it still exists (a global, a
  static) at exit, so it *could* have been freed; usually benign (the OS reclaims it).

Telling them apart needs **reachability analysis**: at report time, treat the live blocks like a
garbage collector would — scan the **roots** (globals, thread stacks, registers) for pointers, follow
them transitively through the live blocks, and mark everything reachable. The **unreachable** live
blocks are the real leaks ("definitely lost"); the reachable ones are "still reachable." LeakSanitizer
does exactly this mark-and-sweep at exit — it's a stop-the-world conservative GC that *reports* instead
of *collects*. (Valgrind's `--leak-check=full` reports the same categories, `[MEM §13.1]`.)

So the honest picture: a leak *detector* that reports at process exit needs reachability to avoid
crying wolf; a leak *tracker* that answers "what's outstanding right now?" over a controlled region of
code does not. Your exercise is the latter — deterministic and false-positive-free because the test
controls exactly what is and isn't freed.

## 3. Double-free and invalid-free fall out for free

The same table that finds leaks also catches two of ASan's `free`-side bugs with no extra machinery:
- **double-free:** `untrack(p)` for a `p` that's already been untracked (not in the table) → the block
  was freed twice.
- **invalid-free:** `untrack(p)` for a `p` that was never tracked (not returned by this allocator) →
  freeing a bogus/offset/stack pointer.

Both are "erase a key that isn't present." Your `untrack` returns whether the pointer was live; the
exercise counts these as `invalid_frees`. (ASan catches the same via its quarantine + shadow — "double
free or corruption" — §16.2; you catch it with the side table.)

## 4. The limits of leak tracking
- It finds blocks that are *never freed*, not blocks freed *too late* (a slow leak that eventually
  frees isn't flagged), nor **logical** leaks (a cache that grows unboundedly but whose entries are all
  "reachable" — e.g. the Lesson-12.3 `shared_ptr` **cycle**, which LSan reports as still-reachable, not
  lost, so it can slip through). Growing-RSS-with-no-LSan-error is the signature (Lesson 7.4 §4).
- It needs the reachability pass (or a controlled scope) to be false-positive-free (§2).
- It says *what* leaked and (with a backtrace, §16.1) *where it was allocated* — not *why* it wasn't
  freed; that's your debugging.

## Drills
1. Classify each as "definitely lost," "still reachable," or "not a leak": (a) `new int` with the only
   pointer overwritten; (b) `new int` stored in a global never freed; (c) a `std::string` local that
   goes out of scope. Which does LSan report, and which would a naïve exit-time counter wrongly report?
2. Why can't a leak detector catch a `shared_ptr` reference cycle as "definitely lost" (Lesson 12.3)?
   What category does it land in, and how else would you find it?
3. Explain how the *same* side table gives you leak detection, double-free, and invalid-free with one
   data structure (§3). What does `untrack` return in each case?
4. Your exercise reports "outstanding right now," not at exit, so it needs no reachability pass. What
   must the *test* do to keep that honest (and ASan-clean) — i.e. why must it eventually free what it
   allocated even while demonstrating the leak?

## My summary
