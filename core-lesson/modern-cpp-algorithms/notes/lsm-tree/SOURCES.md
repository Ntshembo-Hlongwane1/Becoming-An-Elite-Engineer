# Sources

Every factual claim in these notes is tagged with one of the keys below, e.g. `[OSTEP-37]`.
Where a claim is a *derivation* (arithmetic, a consequence I work out in front of you), it is
marked **(derived)** so you know it is reasoning you can check, not a quoted fact.
Where I ran an experiment on this machine, it is marked **(measured)** and the code is shown.

If a claim has no tag and is not marked derived/measured, treat it as a bug in the notes and
check it yourself.

## Primary papers

| Key | Source |
|---|---|
| `[ONEIL96]` | P. O'Neil, E. Cheng, D. Gawlick, E. O'Neil. *The Log-Structured Merge-Tree (LSM-Tree).* Acta Informatica 33(4), 1996. https://www.cs.umb.edu/~poneil/lsmtree.pdf — **the** original paper. |
| `[LUO20]` | C. Luo, M. J. Carey. *LSM-based Storage Techniques: A Survey.* VLDB Journal 29, 2020. https://arxiv.org/abs/1812.07527 — the best single overview of modern LSM-trees; source of the cost table. |
| `[BIGTABLE06]` | F. Chang et al. *Bigtable: A Distributed Storage System for Structured Data.* OSDI 2006. https://static.googleusercontent.com/media/research.google.com/en//archive/bigtable-osdi06.pdf — origin of the words *memtable*, *SSTable*, *minor/major compaction*. |
| `[PUGH90]` | W. Pugh. *Skip Lists: A Probabilistic Alternative to Balanced Trees.* CACM 33(6), 1990. https://15721.courses.cs.cmu.edu/spring2018/papers/08-oltpindexes1/pugh-skiplists-cacm1990.pdf |

## Production systems (docs + source code)

| Key | Source |
|---|---|
| `[LDB-IMPL]` | LevelDB, *doc/impl.md* — https://github.com/google/leveldb/blob/main/doc/impl.md |
| `[LDB-TABLE]` | LevelDB, *doc/table_format.md* — https://github.com/google/leveldb/blob/main/doc/table_format.md |
| `[LDB-LOG]` | LevelDB, *doc/log_format.md* — https://github.com/google/leveldb/blob/main/doc/log_format.md |
| `[LDB-DOC]` | LevelDB, *doc/index.md* (user documentation: writes, sync, snapshots, concurrency) — https://github.com/google/leveldb/blob/main/doc/index.md |
| `[LDB-SRC:<file>]` | LevelDB source file, e.g. `[LDB-SRC:db/skiplist.h]`, `[LDB-SRC:util/arena.cc]` — https://github.com/google/leveldb/tree/main |
| `[ROCKS-OVERVIEW]` | RocksDB wiki, *RocksDB Overview* — https://github.com/facebook/rocksdb/wiki/RocksDB-Overview |
| `[ROCKS-BASIC]` | RocksDB wiki, *Basic Operations* — https://github.com/facebook/rocksdb/wiki/Basic-Operations |
| `[ROCKS-LEVELED]` | RocksDB wiki, *Leveled Compaction* — https://github.com/facebook/rocksdb/wiki/Leveled-Compaction |
| `[ROCKS-UNIVERSAL]` | RocksDB wiki, *Universal Compaction* — https://github.com/facebook/rocksdb/wiki/Universal-Compaction |
| `[ROCKS-MANIFEST]` | RocksDB wiki, *MANIFEST* — https://github.com/facebook/rocksdb/wiki/MANIFEST |
| `[ROCKS-WAL]` | RocksDB wiki, *Write Ahead Log File Format* — https://github.com/facebook/rocksdb/wiki/Write-Ahead-Log-File-Format |
| `[ROCKS-DELETE]` | RocksDB wiki, *Delete Stale Files* — https://github.com/facebook/rocksdb/wiki/Delete-Stale-Files |
| `[ROCKS-BLOOM]` | RocksDB wiki, *RocksDB Bloom Filter* — https://github.com/facebook/rocksdb/wiki/RocksDB-Bloom-Filter |
| `[PB-ENC]` | Protocol Buffers, *Encoding* (varints) — https://protobuf.dev/programming-guides/encoding/ |

## Operating systems & hardware

| Key | Source |
|---|---|
| `[OSTEP-17]` | Arpaci-Dusseau, *Operating Systems: Three Easy Pieces*, ch. 17 *Free-Space Management* — https://pages.cs.wisc.edu/~remzi/OSTEP/vm-freespace.pdf |
| `[OSTEP-26]` | OSTEP ch. 26 *Concurrency: An Introduction* — https://pages.cs.wisc.edu/~remzi/OSTEP/threads-intro.pdf |
| `[OSTEP-37]` | OSTEP ch. 37 *Hard Disk Drives* — https://pages.cs.wisc.edu/~remzi/OSTEP/file-disks.pdf |
| `[OSTEP-42]` | OSTEP ch. 42 *Crash Consistency: FSCK and Journaling* — https://pages.cs.wisc.edu/~remzi/OSTEP/file-journaling.pdf |
| `[OSTEP-43]` | OSTEP ch. 43 *Log-structured File Systems* — https://pages.cs.wisc.edu/~remzi/OSTEP/file-lfs.pdf |
| `[OSTEP-44]` | OSTEP ch. 44 *Flash-based SSDs* — https://pages.cs.wisc.edu/~remzi/OSTEP/file-ssd.pdf |
| `[OSTEP-45]` | OSTEP ch. 45 *Data Integrity and Protection* — https://pages.cs.wisc.edu/~remzi/OSTEP/file-integrity.pdf |
| `[KERNEL-MM]` | Linux kernel docs, *Concepts overview* (virtual memory, pages, page cache) — https://docs.kernel.org/admin-guide/mm/concepts.html |
| `[MAN-open]`, `[MAN-write]`, `[MAN-pread]`, `[MAN-fsync]`, `[MAN-rename]` | Linux man-pages, https://man7.org/linux/man-pages/ (section 2) |
| `[LWN-DURABLE]` | J. Moyer, *Ensuring data reaches disk*, LWN.net, 2011 — https://lwn.net/Articles/457667/ |
| `[PG-FSYNC]` | PostgreSQL wiki, *Fsync Errors* ("fsyncgate 2018") — https://wiki.postgresql.org/wiki/Fsync_Errors |

## C++

| Key | Source |
|---|---|
| `[CPPREF-object]` | cppreference, *Object* (alignment, padding) — https://en.cppreference.com/w/cpp/language/object |
| `[CPPREF-new]` | cppreference, *new expression* (placement new) — https://en.cppreference.com/w/cpp/language/new |
| `[CPPREF-reinterpret]` | cppreference, *reinterpret_cast* (type aliasing) — https://en.cppreference.com/w/cpp/language/reinterpret_cast |
| `[CPPREF-bit_cast]` | cppreference, *std::bit_cast* — https://en.cppreference.com/w/cpp/numeric/bit_cast |
| `[CPPREF-endian]` | cppreference, *std::endian* — https://en.cppreference.com/w/cpp/types/endian |
| `[CPPREF-atomic]` | cppreference, *std::atomic* — https://en.cppreference.com/w/cpp/atomic/atomic |
| `[CPPREF-memorder]` | cppreference, *std::memory_order* — https://en.cppreference.com/w/cpp/atomic/memory_order |
| `[CPPREF-mbr]` | cppreference, *std::pmr::monotonic_buffer_resource* — https://en.cppreference.com/w/cpp/memory/monotonic_buffer_resource |
| `[CPPREF-aligned_alloc]` | cppreference, *std::aligned_alloc* — https://en.cppreference.com/w/cpp/memory/c/aligned_alloc |
| `[CPPREF-operator_new]` | cppreference, *operator new* — https://en.cppreference.com/w/cpp/memory/new/operator_new |
| `[CPPREF-raii]` | cppreference, *RAII* — https://en.cppreference.com/w/cpp/language/raii |
| `[CPPREF-hash]` | cppreference, *std::hash* — https://en.cppreference.com/w/cpp/utility/hash |
| `[CPPREF-vector-at]` | cppreference, *std::vector::operator[]* — https://en.cppreference.com/w/cpp/container/vector/operator_at |
| `[CPPREF-hdis]` | cppreference, *hardware_destructive_interference_size* — https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size |

## Your books

| Key | Source |
|---|---|
| `[ALGO §x.y]` | A. Alheraki, *Modern C++ Algorithms: A Graduate-Level Companion*, 2025 (section number) |
| `[MEM §x.y]` | A. Alheraki, *Advanced Memory Management in Modern C++*, 2nd ed. (section number) |

## Machine facts (measured on this VM, 2026-10-04)

| Fact | Command | Value |
|---|---|---|
| Virtual memory page size | `getconf PAGESIZE` | 4096 bytes |
| Byte order | `lscpu` | Little Endian |
| L1 cache line size | `cat /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size` | 64 bytes |
| CPU | `lscpu` | Intel Core i5-9600K |
| Compiler | `g++ --version` | GCC 15.2.0 |
