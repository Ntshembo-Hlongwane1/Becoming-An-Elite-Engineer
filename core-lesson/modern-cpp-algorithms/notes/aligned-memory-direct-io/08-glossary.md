# Glossary

| Term | One line | Where |
|---|---|---|
| **bit / byte** | one binary digit / 8 bits | P1 §1 |
| **hexadecimal** | base 16; one hex digit = 4 bits; `0x1000` = 4096 | P1 §2 |
| **address** | index of a byte in the (virtual) address space | P1 §3 |
| **virtual address** | per-process address translated to physical RAM page by page | P1 §3 |
| **`std::uintptr_t`** | unsigned integer that can hold a pointer's value, for address arithmetic | P1 §4 |
| **power of two** | a number with exactly one bit set | P1 §5 |
| **mask** | `N - 1` for power-of-two N: log₂N low one-bits | P1 §5 |
| **N-aligned** | address/size is a multiple of N; for power-of-two N, the low log₂N bits are zero | P1 §6 |
| **natural alignment** | the largest power of two that divides an address (`1 << countr_zero(addr)`) | P1 §14 |
| **align down** | `x & ~(N-1)`: largest multiple of N ≤ x | P1 §8 |
| **align up** | `(x + N - 1) & ~(N-1)`: smallest multiple of N ≥ x; **can overflow** | P1 §8 |
| **`alignof(T)`** | alignment requirement of type T | P1 §10 |
| **`alignas(N)`** | require N-alignment for a variable, member or type | P1 §10 |
| **`std::max_align_t`** | type with the largest fundamental alignment (16 here) | P1 §10 |
| **fundamental / extended alignment** | ≤ `alignof(max_align_t)` / larger than that | P1 §10 |
| **over-aligned type** | a type with extended alignment (e.g. `alignas(64)`) | P1 §10 |
| **cache line** | unit of CPU↔RAM transfer; 64 bytes here | P1 §11 |
| **false sharing** | two threads' hot data on one cache line slowing each other | P1 §13 |
| **padding** | unused bytes the compiler inserts to satisfy alignment | P1 §13 |
| **undefined behaviour (UB)** | the standard imposes no requirements; the compiler may assume it never happens | P1 §12 |
| **UBSan / ASan** | compiler sanitizers for UB / memory errors (`-fsanitize=undefined,address`) | P1 §12, P3 §8 |
| **chunk header** | glibc's size fields stored just before the pointer `malloc` returns | P2 §5 |
| **mmap threshold** | size above which glibc `malloc` uses `mmap` (128 KiB initially, dynamic) | P2 §6 |
| **`__STDCPP_DEFAULT_NEW_ALIGNMENT__`** | alignment `operator new(size_t)` guarantees (16 here) | P2 §7 |
| **`std::align_val_t`** | scoped enum carrying an alignment to aligned `operator new/delete` | P3 §5 |
| **`std::aligned_alloc`** | C/C++17 aligned allocation; size multiple of alignment; release with `free` | P3 §3 |
| **`posix_memalign`** | POSIX aligned allocation; returns error code, doesn't set errno | P3 §4 |
| **`std::align`** | moves a pointer forward inside a buffer to an aligned spot; modifies `ptr` and `space` | P3 §7b |
| **over-allocate and adjust** | ask for N + A − 1, round up; keep the original pointer for release | P3 §7 |
| **bad free** | releasing a pointer the allocator didn't return | P3 §8, P7 §2 |
| **RAII** | resource lifetime tied to object lifetime; destructor releases | P4 §1 |
| **custom deleter** | the callable `unique_ptr` uses to release; stateless struct costs 0 bytes | P4 §2 |
| **rule of five** | if you write a destructor, decide copy/move ctor and assignment explicitly | P4 §3, P6 B1 |
| **Allocator (named requirement)** | `value_type`, `allocate`, `deallocate`, `==`, converting ctor | P4 §4 |
| **`rebind`** | how a container gets an allocator for another type (nodes); mandatory with non-type params | P4 §5 |
| **`std::pmr::memory_resource`** | run-time polymorphic allocator interface; alignment is an argument | P4 §6 |
| **`std::span`** | non-owning `{pointer, length}` view | P4 §7 |
| **sector / logical block size** | smallest unit a device reads/writes (512 here) | P5 §1 |
| **filesystem block** | unit of file space allocation (4096 on your ext4) | P5 §1 |
| **page / page cache** | 4096-byte memory unit / kernel's in-RAM copy of file data | P5 §1–2 |
| **DMA** | device moves data to/from memory without the CPU copying it | P5 §3 |
| **`O_DIRECT`** | bypass the page cache; device DMAs from your buffer; alignment rules apply | P5 §4–5 |
| **the three rules** | buffer address, length, file offset: each a multiple of the DIO alignment | P5 §5 |
| **`statx(STATX_DIOALIGN)`** | query `stx_dio_mem_align` / `stx_dio_offset_align` (Linux ≥ 6.1) | P5 §6 |
| **tail block** | last partial block: pad with zeros, write, `ftruncate` to logical size, rewrite later | P5 §7 |
| **`O_SYNC` / `O_DSYNC` / `fdatasync`** | durability; **not** implied by `O_DIRECT` | P5 §9 |
| **`pwrite` / `pread`** | positional I/O; no shared file offset | P5 §13 |
| **data remanence** | bytes remaining on storage after logical deletion/truncation | P7 §4 |
| **partial pointer overwrite** | overwriting only low bytes of a pointer; works because ASLR keeps low 12 bits | P7 §7 |
