# Exercise 3 — The Process Address Space (a /proc/self/maps parser)

Implement `src/maps.cpp` until `./run.sh` prints `ALL TESTS PASSED`.
Notes: `../../notes/03-process-address-space/`.

You build the lookup a debugger, the kernel fault handler, and your capstone all perform: parse the
kernel's VMA list, binary-search for the VMA containing an address, and classify it.

Provided: `region_name()` (boilerplate enum→string). You implement `parse_maps_line`, `parse_maps`,
`find_vma`, `classify`. Full contracts in `include/mm/maps.hpp`.

The last test, `classify_real_process_addresses`, runs your classifier against this process's own
`/proc/self/maps` and checks a stack variable is `kStack`, a function is `kFileExec`, etc. If
`/proc` isn't available it SKIPs.

Run: `./run.sh` or `./run.sh <filter>`.
