# Exercise 4 — Virtual Memory (page math + /proc/status + a demand-paging prober)

Implement `src/vm.cpp` until `./run.sh` prints `ALL TESTS PASSED`.
Notes: `../../notes/04-virtual-memory/`.

Three groups (contracts in `include/mm/vm.hpp`):
- **page arithmetic** — `page_base`/`page_offset`/`pages_spanned` with masks (Lesson 4.1 §2, Lesson 1 §7-8).
- **/proc/self/status** — `parse_status_kb` (Lesson 4.2 §4).
- **PageProbe** — an anonymous `mmap` + `mincore` prober that demonstrates demand paging
  (Lesson 4.2 §5): fresh pages are not resident; touching a page makes it resident.

`probe_demand_paging` is the measured heart of the lesson turned into a test. ASan+UBSan are on, so
the `mmap`/`munmap` in `PageProbe` must be balanced (the destructor must unmap).

Run: `./run.sh` or `./run.sh <filter>`.
