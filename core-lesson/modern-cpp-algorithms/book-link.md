file:///home/vboxuser/Downloads/Modern_CPP_Algorithms.pdf
file:///home/vboxuser/Downloads/Advanced_Memory_Management_in_Modern_CPP_Second_Edition.pdf

# How these two books are used here

- **Modern C++ Algorithms: A Graduate-Level Companion** (Ayman Alheraki, simplifycpp.org, Aug 2025)
  is the *main* DSA book. Every DSA topic is approached from a systems-engineer point of view:
  "what does this structure cost in memory, cache lines, syscalls, and disk pages?"
- **Advanced Memory Management in Modern C++, 2nd ed.** (same publisher) is the *memory* companion:
  allocation, pools, alignment, smart pointers, atomics, memory model.

Topics the books do not cover (e.g. LSM-trees) are taught in `notes/` from primary sources
(papers, official docs, man pages, production source code), and every lesson points back to the
book chapters it builds on.

Layout:

```
modern-cpp-algorithms/
├── book-link.md            <- this file
├── notes/
│   └── lsm-tree/           <- the LSM-tree build-up lesson (start at 00-README.md)
└── exercises/
    └── lsm-tree/           <- Exercise 1 (in-memory LSM) and Exercise 2 (on-disk LSM)
```
