# Sources

Every factual claim in these notes is tagged with one of the keys below. **(derived)** = reasoning
you can check step by step. **(measured)** = I ran it on your VM; the program is in the notes so you
can re-run it. A claim with no tag and no marker is a bug in the notes — check it yourself.

## Linux man-pages (also on your machine: `man 2 open`, etc.)

| Key | Source |
|---|---|
| `[MAN-open]` | `open(2)` — the `O_DIRECT` flag description and the long *NOTES → O_DIRECT* section. https://man7.org/linux/man-pages/man2/open.2.html |
| `[MAN-statx]` | `statx(2)` — `STATX_DIOALIGN`, `stx_dio_mem_align`, `stx_dio_offset_align`. https://man7.org/linux/man-pages/man2/statx.2.html |
| `[MAN-pwrite]` | `pread(2)` / `pwrite(2)` — positional I/O. https://man7.org/linux/man-pages/man2/pwrite.2.html |
| `[MAN-write]` | `write(2)` — partial writes, `EINTR`. https://man7.org/linux/man-pages/man2/write.2.html |
| `[MAN-fsync]` | `fsync(2)` / `fdatasync(2)`. https://man7.org/linux/man-pages/man2/fsync.2.html |
| `[MAN-ftruncate]` | `truncate(2)` / `ftruncate(2)`. https://man7.org/linux/man-pages/man2/ftruncate.2.html |
| `[MAN-mmap]` | `mmap(2)` — page-aligned results. https://man7.org/linux/man-pages/man2/mmap.2.html |
| `[MAN-malloc]` | `malloc(3)` — "suitably aligned for any type that fits into the requested size or less". https://man7.org/linux/man-pages/man3/malloc.3.html |
| `[MAN-posix_memalign]` | `posix_memalign(3)` — power of two *and* multiple of `sizeof(void*)`; returns the error, does not set `errno`. https://man7.org/linux/man-pages/man3/posix_memalign.3.html |
| `[MAN-aligned_alloc]` | `aligned_alloc(3)` (glibc). https://man7.org/linux/man-pages/man3/aligned_alloc.3.html |
| `[MAN-mallopt]` | `mallopt(3)` — `M_MMAP_THRESHOLD`, default 128 KiB, dynamic threshold. https://man7.org/linux/man-pages/man3/mallopt.3.html |
| `[MAN-madvise]` | `madvise(2)` — `MADV_DONTFORK`. https://man7.org/linux/man-pages/man2/madvise.2.html |

## C++ reference & standard papers

| Key | Source |
|---|---|
| `[CPPREF-object]` | cppreference, *Object* → Alignment (definition, fundamental vs extended, UB on misaligned storage, padding example). https://en.cppreference.com/w/cpp/language/object |
| `[CPPREF-alignas]` | cppreference, *alignas specifier*. https://en.cppreference.com/w/cpp/language/alignas |
| `[CPPREF-alignof]` | cppreference, *alignof operator*. https://en.cppreference.com/w/cpp/language/alignof |
| `[CPPREF-max_align_t]` | cppreference, *std::max_align_t*. https://en.cppreference.com/w/cpp/types/max_align_t |
| `[CPPREF-uintptr_t]` | cppreference, *Fixed width integer types* (`std::uintptr_t`, optional). https://en.cppreference.com/w/cpp/types/integer |
| `[CPPREF-align]` | cppreference, *std::align* — signature `void* align(size_t alignment, size_t size, void*& ptr, size_t& space)`. https://en.cppreference.com/w/cpp/memory/align |
| `[CPPREF-aligned_alloc]` | cppreference, *std::aligned_alloc* — size must be a multiple of alignment; free with `std::free`; DR460; MSVC lacks it. https://en.cppreference.com/w/cpp/memory/c/aligned_alloc |
| `[CPPREF-operator_new]` | cppreference, *operator new* — `std::align_val_t` overloads (C++17). https://en.cppreference.com/w/cpp/memory/new/operator_new |
| `[CPPREF-operator_delete]` | cppreference, *operator delete* — aligned and sized overloads. https://en.cppreference.com/w/cpp/memory/new/operator_delete |
| `[CPPREF-allocator_traits]` | cppreference, *std::allocator_traits* — `rebind_alloc` rule ("zero or more **type** arguments"), `is_always_equal`. https://en.cppreference.com/w/cpp/memory/allocator_traits |
| `[CPPREF-Allocator]` | cppreference, named requirement *Allocator* (`a == b`, converting constructor). https://en.cppreference.com/w/cpp/named_req/Allocator |
| `[CPPREF-allocator]` | cppreference, *std::allocator* — `construct`/`destroy` removed in C++20. https://en.cppreference.com/w/cpp/memory/allocator |
| `[CPPREF-unique_ptr]` | cppreference, *std::unique_ptr* (custom deleters). https://en.cppreference.com/w/cpp/memory/unique_ptr |
| `[CPPREF-raii]` | cppreference, *RAII*. https://en.cppreference.com/w/cpp/language/raii |
| `[CPPREF-move_if_noexcept]` | cppreference, *std::move_if_noexcept*. https://en.cppreference.com/w/cpp/utility/move_if_noexcept |
| `[CPPREF-span]` | cppreference, *std::span*. https://en.cppreference.com/w/cpp/container/span |
| `[CPPREF-bit]` | cppreference, *<bit>* — `std::has_single_bit`, `std::bit_ceil`, `std::countr_zero`. https://en.cppreference.com/w/cpp/header/bit |
| `[CPPREF-pmr]` | cppreference, *std::pmr::memory_resource*. https://en.cppreference.com/w/cpp/memory/memory_resource |
| `[CPPREF-hdis]` | cppreference, *hardware_destructive_interference_size*. https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size |
| `[P0035]` | C. Nelson, *Dynamic Memory Allocation for Over-Aligned Data*, P0035R4, 2016 (adopted in C++17). https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0035r4.html |
| `[SYSV-ABI]` | *System V Application Binary Interface, AMD64 Architecture Processor Supplement* §3.2.2 (16-byte stack alignment at calls). https://gitlab.com/x86-psABIs/x86-64-ABI |

## glibc

| Key | Source |
|---|---|
| `[GLIBC-malloc.c]` | glibc `malloc/malloc.c` — "malloc_chunk details" comment (chunk header, `mem` pointer). https://sourceware.org/git/?p=glibc.git;a=blob;f=malloc/malloc.c (mirror: https://github.com/bminor/glibc/blob/master/malloc/malloc.c) |
| `[CVE-2013-4332]` | NVD entry: integer overflows in `pvalloc`, `valloc`, `posix_memalign`, `memalign`, `aligned_alloc` in glibc ≤ 2.18. https://nvd.nist.gov/vuln/detail/CVE-2013-4332 |
| `[RH-CVE-2013-4332]` | Red Hat CVE page (impact statement). https://access.redhat.com/security/cve/CVE-2013-4332 |
| `[HOW2HEAP]` | shellphish, *how2heap* — educational heap-exploitation techniques per glibc version (`house_of_spirit.c`). https://github.com/shellphish/how2heap |
| `[GLIBC-15857-FIX]` | glibc commit e-mail with the fix (`if (bytes > SIZE_MAX - alignment - MINSIZE)`). https://www.sourceware.org/ml/glibc-cvs/2013-q3/msg00200.html ; disclosure: https://www.openwall.com/lists/oss-security/2013/09/11/7 |

## Production systems

| Key | Source |
|---|---|
| `[ROCKS-DIO]` | RocksDB wiki, *Direct IO* — why self-caching engines use it; "offset, #bytes and the buffer address must be aligned to the logical sector size". https://github.com/facebook/rocksdb/wiki/Direct-IO |
| `[ROCKS-SRC:aligned_buffer.h]` | RocksDB `util/aligned_buffer.h` — `TruncateToPageBoundary`, `Roundup`, `AllocateNewBuffer` (over-allocate + mask). https://github.com/facebook/rocksdb/blob/main/util/aligned_buffer.h |
| `[ROCKS-SRC:writable_file_writer.cc]` | RocksDB `file/writable_file_writer.cc` — `WriteDirect`: pad tail with zeros, rewrite it later. https://github.com/facebook/rocksdb/blob/main/file/writable_file_writer.cc |
| `[ROCKS-SRC:io_posix.cc]` | RocksDB `env/io_posix.cc` — `IsSectorAligned` asserts before `pread`; `ftruncate(fd_, filesize_)` on close. https://github.com/facebook/rocksdb/blob/main/env/io_posix.cc |
| `[PG-c.h]` | PostgreSQL `src/include/c.h` — `PGIOAlignedBlock { alignas(PG_IO_ALIGN_SIZE) char data[BLCKSZ]; }`. https://github.com/postgres/postgres/blob/master/src/include/c.h |
| `[PG-COMMIT]` | PostgreSQL commit faeedbcef, *Introduce PG_IO_ALIGN_SIZE and align all I/O buffers*: "There is no standard governing O_DIRECT's requirements so we might eventually have to reconsider this". https://github.com/postgres/postgres/commit/faeedbcefd40bfdf314e048c425b6d9208896d90 |
| `[PG-config]` | PostgreSQL `src/include/pg_config_manual.h` — `#define PG_IO_ALIGN_SIZE 4096` "4K corresponds to common sector and memory page size". https://github.com/postgres/postgres/blob/master/src/include/pg_config_manual.h |
| `[LDB-SRC:arena]` | LevelDB `util/arena.cc` — `AllocateAligned` with the `& (align-1)` trick. https://github.com/google/leveldb/blob/main/util/arena.cc |

## Operating systems

| Key | Source |
|---|---|
| `[OSTEP-36]` | Arpaci-Dusseau, *OSTEP* ch. 36 *I/O Devices* (§36.5 *More Efficient Data Movement With DMA*). https://pages.cs.wisc.edu/~remzi/OSTEP/file-devices.pdf |
| `[OSTEP-13]` | OSTEP ch. 13 *The Abstraction: Address Spaces* (virtual addresses). https://pages.cs.wisc.edu/~remzi/OSTEP/vm-intro.pdf |
| `[KERNEL-MM]` | Linux kernel docs, *Concepts overview* (pages, page cache). https://docs.kernel.org/admin-guide/mm/concepts.html |
| `[LWN-DURABLE]` | J. Moyer, *Ensuring data reaches disk*, LWN.net 2011. https://lwn.net/Articles/457667/ |

## Your books

| Key | Source |
|---|---|
| `[MEM §x.y]` | A. Alheraki, *Advanced Memory Management in Modern C++*, 2nd ed., 2025 |
| `[ALGO §x.y]` | A. Alheraki, *Modern C++ Algorithms*, 2025 |

## Earlier lessons in this repo

| Key | Source |
|---|---|
| `[LSM-P1]`, `[LSM-P2]` | `../lsm-tree/01-the-machine-from-zero.md`, `../lsm-tree/02-cpp-memory-from-zero.md` |

## Machine facts (measured on your VM, 2026-10-07)

| Fact | Command / program | Value |
|---|---|---|
| Kernel | `uname -r` | 7.0.0-31-generic |
| Compiler / libc | `g++ --version`, `ldd --version` | GCC 15.2.0, glibc 2.43 |
| Page size | `getconf PAGESIZE` | 4096 |
| Cache line | `/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size` | 64 |
| Repo filesystem | `df -T .` | ext4 on `/dev/sda2` |
| `/tmp` filesystem | `df -T /tmp` | tmpfs |
| Disk logical / physical sector | `lsblk -o NAME,LOG-SEC,PHY-SEC` | 512 / 512 (VirtualBox virtual disk) |
| FS block size | `stat -f -c %S .` | 4096 |
| Direct-I/O alignment (ext4 file) | `statx(STATX_DIOALIGN)` (Part 5 §6) | mem 512, offset 512 |
| Direct-I/O alignment (tmpfs file) | same | not reported; misaligned I/O accepted |
| `alignof(std::max_align_t)` | Part 2 §3 | 16 |
| `__STDCPP_DEFAULT_NEW_ALIGNMENT__` | Part 2 §4 | 16 |
| `std::hardware_destructive_interference_size` | Part 2 §3 | 64 |
