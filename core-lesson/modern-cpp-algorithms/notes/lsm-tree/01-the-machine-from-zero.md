# Part 1 — The Machine, From Zero

> Why start with hardware? Because the LSM-tree's own authors justify it entirely in terms of
> hardware cost. The 1996 paper opens with disk-arm economics, not with algorithms `[ONEIL96 §1]`.
> A data structure on storage is an *answer to a machine*. If you don't know the machine, the
> answer looks arbitrary.

Contents

1. Memory is an array of bytes
2. Virtual memory and *pages*
3. CPU caches and *cache lines*
4. Hard disk drives: sectors, seeks, and why "sequential" is king
5. SSDs: flash pages, erase blocks, and the log-structured device inside your device
6. The journey of a `write()`: every buffer between your variable and the platter
7. The system calls you will use (and their traps)
8. Durability: the `fsync` + `rename` dance
9. When the stack lies: write caches, barriers, and "fsyncgate"
10. Bits rot: data-integrity failures and checksums
11. Summary table + drills

---

## 1. Memory is an array of bytes

- A **bit** is a 0 or 1. A **byte** is 8 bits, so it can hold 2⁸ = 256 different values
  (0–255) **(derived)**.
- Your program sees memory as one long array of bytes. Each byte has a number — its
  **address**. A pointer in C++ is just such a number with a type attached.
- Bigger values (a 4-byte `uint32_t`, an 8-byte `uint64_t`) occupy several consecutive
  addresses. *Which* byte of the number goes at the lowest address is called **endianness** —
  covered in Part 2 because it bites the moment you write numbers to a file.

That is the programmer's view. The next two sections show it is an illusion maintained by the
OS and the CPU, and that the illusion has a *granularity* — pages and cache lines — which every
storage structure is designed around.

## 2. Virtual memory and pages

From the Linux kernel documentation `[KERNEL-MM]`:

- Physical RAM "is not necessarily contiguous" and differs between machines, so the OS gives each
  program **virtual memory**: "each and every memory access uses a virtual address" which the CPU
  "translates … to a physical address".
- "The physical system memory is divided into page frames, or pages. The size of each page is
  architecture specific."
- Translation is done through **page tables** organised hierarchically.

On this machine:

```bash
$ getconf PAGESIZE
4096
```
**(measured)** — a page is **4096 bytes (4 KiB)**.

Why this matters for us:

- The OS moves memory to/from disk, caches files, and protects memory **in whole pages**. When
  you read *one byte* of a file, the kernel brings in (at least) a whole page.
- The **page cache** (§6) is literally a cache of file contents in units of pages
  `[KERNEL-MM]`.
- That is why storage engines choose block sizes like 4 KiB: LevelDB's default data-block size is
  `block_size = 4 * 1024` `[LDB-SRC:include/leveldb/options.h]`, and your algorithms book says a
  B-tree node "should fit into a single disk block (commonly 4KB or 8KB)" `[ALGO §11.1.2]`.

> **Systems-engineer reflex #1:** whenever you design a record or a node, ask
> "how many pages does one lookup touch?" — not "how many comparisons?".

## 3. CPU caches and cache lines

RAM is far slower than the CPU, so the CPU keeps small, fast copies of recently used memory in
**caches**. Your algorithms book `[ALGO §22.1.1]`:

- **Spatial locality**: "Accessing contiguous memory locations improves cache line utilization."
- **Temporal locality**: "Reusing recently accessed data avoids cache misses."
- **Cache line size**: "Typically 64 bytes in modern x86 and ARM CPUs."

The memory book says the same in `[MEM §9.3]`.

On this machine:

```bash
$ cat /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size
64
```
**(measured)**

So there are *two* granularities stacked on top of each other:

| Unit | Size here | Who moves it | Between |
|---|---|---|---|
| cache line | 64 B | CPU hardware | RAM ↔ CPU cache |
| page | 4096 B | OS kernel | disk ↔ RAM (page cache), and for virtual memory |
| device block / flash page | ~512 B–4 KiB (see §4–5) | disk firmware | media ↔ disk controller |

Each LSM component is designed for one of these:

- The **memtable** lives in RAM → cache lines matter (pointer chasing in a skip list = cache
  misses; Part 3).
- **SSTable blocks** live on disk → pages matter.
- **Bloom filters**: RocksDB's newer "full filter" format "limits the probe bits for a key to be
  all within the same CPU cache line … limiting the CPU cache misses to one per key"
  `[ROCKS-BLOOM]`. That is a data structure redesigned *purely* for cache lines.

## 4. Hard disk drives: sectors, seeks, and why "sequential" is king

All facts in this section are from OSTEP ch. 37 `[OSTEP-37]`.

**The interface.** A disk is "a large number of sectors (512-byte blocks), each of which can be
read or written … numbered from 0 to n − 1". So to the OS a disk is *also* an array — of
512-byte sectors.

**Atomicity — the most important sentence in this whole Part:**
> "the only guarantee drive manufacturers make is that a single 512-byte write is atomic (i.e.,
> it will either complete in its entirety or it won't complete at all); thus, if an untimely
> power loss occurs, only a portion of a larger write may complete (sometimes called a
> **torn write**)." `[OSTEP-37]`

Hold on to that. A 4 KiB B-tree node written in place spans 8 sectors; a power cut can leave
4 new sectors and 4 old ones **(derived)**. Every on-disk format in Part 6 is built so that a
torn write is *detectable* (checksums) and *harmless* (never overwrite live data).

**The mechanics.** Data sits on spinning platters in concentric *tracks*; an arm moves a head.
One I/O costs:

```
T_I/O = T_seek (move the arm) + T_rotation (wait for the sector to spin under the head) + T_transfer
```

OSTEP's worked example, Seagate Cheetah 15K.5 (a fast server drive) `[OSTEP-37]`:

- 4 KB random read: T_seek = 4 ms, T_rotation = 2 ms, T_transfer = 30 µs → ≈ 6 ms per I/O →
  **0.66 MB/s**.
- 100 MB sequential read: one seek + one rotation, then streaming → ≈ **125 MB/s**.

| | Cheetah 15K.5 | Barracuda |
|---|---|---|
| Random 4 KB | 0.66 MB/s | 0.31 MB/s |
| Sequential | 125 MB/s | 105 MB/s |

"there is a huge gap in drive performance between random and sequential workloads, almost a
factor of 200 or so for the Cheetah and more than a factor 300 difference for the Barracuda"
`[OSTEP-37]`, and OSTEP's design tip: "When at all possible, transfer data to and from disks in
a sequential manner. If sequential is not possible, at least think about transferring data in
large chunks: the bigger, the better."

> This 200–300× gap **is** the reason the LSM-tree exists. O'Neil's paper calls the cost of
> random I/O the cost of "disk arms" and designs everything so that writes happen in
> "multi-page blocks" to "eliminat[e] seek time and rotational latency" `[ONEIL96 §2.1]`.

## 5. SSDs: flash pages, erase blocks, and the log inside your device

Everything here is from OSTEP ch. 44 `[OSTEP-44]`. "No moving parts" does **not** mean "random
writes are free".

- Flash is organised into **blocks** ("erase blocks … typically of size 128 KB or 256 KB") made
  of **pages** ("a few KB in size (e.g., 4KB)").
- Three operations: **read** a page; **erase** a whole block ("quite expensive, taking a few
  milliseconds"); **program** a page (only possible on an erased page). "Once a page has been
  programmed, the only way to change its contents is to erase the entire block."
- So you **cannot overwrite** a 4 KB page in place. To change it, a naive device must read the
  whole 128–256 KB block, erase it, and program it back — "severe **write amplification**
  (proportional to the number of pages in a block)".
- Repeated erase/program **wears out** a block; devices do **wear leveling** to spread writes.
- Real SSDs therefore contain a **Flash Translation Layer (FTL)** that is **log-structured**:
  new data is written to a fresh page and a mapping table is updated; old versions become
  "garbage" that **garbage collection** must reclaim — and "excessive garbage collection drives
  up write amplification and lowers performance".

**(derived)** Notice the shape: *never overwrite in place → append new versions → background
garbage collection*. That is the LSM-tree's shape too. An LSM-tree that writes large,
sequential, immutable files is friendly to the FTL; a structure that rewrites random 4 KB pages
in place makes the FTL do read-erase-program work. RocksDB states its "initial focus [was] on
fast storage (especially Flash storage)" `[ROCKS-OVERVIEW]`, and the survey notes high write
amplification "reduces the lifespan of SSDs" `[LUO20 §3.1]`.

New vocabulary — **write amplification** = bytes physically written ÷ bytes the user asked to
write. You will meet it again at three layers (FTL, file system, LSM compaction).

## 6. The journey of a `write()`

Jeff Moyer's LWN article `[LWN-DURABLE]` describes the layers data passes through:

```
 your variable / buffer          (application memory)
        │  fwrite()                    ← "stream I/O" (libc), may NOT make a syscall
 libc FILE* buffer                (still application memory)
        │  fflush() → write()          ← "system I/O" (syscall)
 kernel PAGE CACHE                (kernel memory; pages marked "dirty")
        │  writeback (whenever the kernel decides) or fsync()
 device volatile WRITE CACHE      (RAM on the disk controller)
        │  cache flush command (issued by fsync)
 non-volatile media               ← only now is it "safe"
```

Key quotes:

- The page cache is "a write-back cache … Dirty pages can live in the page cache for an
  indeterminate amount of time" `[LWN-DURABLE]`.
- The kernel docs: "when one writes to a file, the data is placed in the page cache and
  eventually gets into the backing storage device. The written pages are marked as dirty"
  `[KERNEL-MM]`.
- "The storage device may further buffer the data in a volatile write-back cache. If power is
  lost while data is in this cache, the data will be lost." `[LWN-DURABLE]`
- `O_DIRECT` "bypass[es] the kernel's page cache … [but] the storage may itself store the data in
  a write-back cache, so fsync() is still required for files opened with O_DIRECT"
  `[LWN-DURABLE]`.

> **Systems-engineer reflex #2:** "returned successfully" ≠ "durable". Always ask *which layer*
> the data is in when the function returns.

## 7. The system calls you will use

A **system call** is a request from your program to the kernel. These are the ones an LSM
engine is built on. Read each man page once in full (`man 2 write`, etc.).

### `open(2)` — get a file descriptor

Returns a small integer, a **file descriptor (fd)**, naming an open file. Flags you will meet:

| Flag | Meaning (from `[MAN-open]`) | Used for |
|---|---|---|
| `O_CREAT` | create if missing | new WAL / SSTable |
| `O_EXCL` | "Ensure that this call creates the file … if path already exists, then open() fails" | never clobber an existing SSTable by accident |
| `O_APPEND` | every write goes to end of file | WAL |
| `O_CLOEXEC` | close the fd automatically across `exec` | LevelDB uses it as `kOpenBaseFlags` `[LDB-SRC:util/env_posix.cc]` |
| `O_DSYNC` | each write behaves "as though each write(2) was followed by a call to fdatasync(2)" | alternative to explicit sync |
| `O_DIRECT` | "Try to minimize cache effects … File I/O is done directly to/from user-space buffers" | engines with their own cache (RocksDB optional `[ROCKS-OVERVIEW]`) |

`O_DIRECT` alignment trap: it "may impose alignment restrictions on the length and address of
user-space buffers and the file offset of I/Os … misaligned O_DIRECT I/Os … can either fail with
EINVAL or fall back to buffered I/O" `[MAN-open]`. This is a real-world reason you will need
**aligned allocation** (Part 2 §2).

An fd is a **kernel resource**. Leak it and you eventually hit the per-process limit — exactly
the bug class in your own `master-file-manager` lesson. The fix is RAII (Part 2 §9).

### `write(2)` — and its two traps

1. **Partial writes.** "a successful write() may transfer fewer than count bytes … the caller can
   make another write() call to transfer the remaining bytes" `[MAN-write]`.
2. **Signals.** "If a write() is interrupted by a signal handler before any bytes are written,
   then the call fails with the error EINTR" `[MAN-write]`.

So a correct write is a *loop*. LevelDB's actual code `[LDB-SRC:util/env_posix.cc]`:

```cpp
Status WriteUnbuffered(const char* data, size_t size) {
  while (size > 0) {
    ssize_t write_result = ::write(fd_, data, size);
    if (write_result < 0) {
      if (errno == EINTR) {
        continue;  // Retry
      }
      return PosixError(filename_, errno);
    }
    data += write_result;
    size -= write_result;
  }
  return Status::OK();
}
```

3. **No durability.** "A successful return from write() does not make any guarantee that data has
   been committed to disk … The only way to be sure is to call fsync(2)" `[MAN-write]`.

LevelDB also buffers small appends in user space (`kWritableFileBufferSize = 65536`
`[LDB-SRC:util/env_posix.cc]`) so it makes fewer syscalls — each syscall has a fixed cost.

### `pread(2)` / `pwrite(2)` — positional I/O

"pread() reads up to count bytes from file descriptor fd at offset offset … The file offset is
not changed." `[MAN-pread]`. Because it doesn't move a shared file position, many threads can
read the same SSTable fd concurrently **(derived)**. Readers of SSTables use positional reads:
"read block at offset X, size N".

### `fsync(2)` / `fdatasync(2)` — make it durable

From `[MAN-fsync]`:

- `fsync()` "transfers ('flushes') all modified in-core data of … the file … to the disk device
  … This includes writing through or flushing a disk cache if present. The call blocks until the
  device reports that the transfer has completed."
- "Calling fsync() does not necessarily ensure that the entry in the directory containing the
  file has also reached disk. For that an explicit fsync() on a file descriptor for the
  directory is also needed."
- `fdatasync()` skips metadata not needed to read the data back (e.g. modification time) but
  **does** flush a changed file size.

### `rename(2)` — the atomic switch

"If newpath already exists, it will be atomically replaced, so that there is no point at which
another process attempting to access newpath will find it missing." `[MAN-rename]`

This is the *only* "all-or-nothing" primitive POSIX gives you for files, and storage engines
build their crash-safety on it (§8). RocksDB says it bluntly: "File system operations are not
atomic … POSIX file system does not support atomic batching of operations either"
`[ROCKS-MANIFEST]`.

## 8. Durability: the `fsync` + `rename` dance

To replace a small file (e.g. a "which files are live?" pointer) crash-safely, LWN gives the
recipe `[LWN-DURABLE]`:

```
1. create a temp file, write the new contents
2. fsync() the temp file
3. rename the temp file to the appropriate name
4. fsync() the containing directory
```

**Why each step (derived):**

- Without (2) the rename could reach disk before the data → after a crash the real name points
  at an empty or partial file.
- (3) is atomic: readers see either the whole old file or the whole new file.
- Without (4) the *rename itself* (a directory change) may not be durable → after a crash you
  might still see the old name.

LevelDB does exactly this for its `CURRENT` file `[LDB-SRC:db/filename.cc]`:

```cpp
Status SetCurrentFile(Env* env, const std::string& dbname, uint64_t descriptor_number) {
  // Remove leading "dbname/" and add newline to manifest file name
  std::string manifest = DescriptorFileName(dbname, descriptor_number);
  ...
  std::string tmp = TempFileName(dbname, descriptor_number);
  Status s = WriteStringToFileSync(env, contents.ToString() + "\n", tmp);   // steps 1+2
  if (s.ok()) {
    s = env->RenameFile(tmp, CurrentFileName(dbname));                      // step 3
  }
  if (!s.ok()) {
    env->RemoveFile(tmp);
  }
  return s;
}
```

…and it fsyncs the *directory* before syncing a MANIFEST, with this comment
`[LDB-SRC:util/env_posix.cc]`:

> "Ensure new files referred to by the manifest are in the filesystem. This needs to happen
> before the manifest file is flushed to disk, to avoid crashing in a state where the manifest
> refers to files that are not yet on disk."

That comment is a whole lesson: **durability is about ordering**. "A must be on disk before B
points to A." You will see this rule — *write the thing, sync it, then publish a pointer to it*
— over and over in Part 6.

## 9. When the stack lies

1. **Disk write caches and barriers.** With a disk write cache enabled, "a disk will inform the OS
   the write is complete when it simply has been placed in the disk's memory cache … thus
   ordering between writes is not preserved." Systems issue **write barriers**, but "some disk
   manufacturers … explicitly ignore write-barrier requests" `[OSTEP-42]`.
2. **macOS.** LevelDB's comment: "On macOS and iOS, fsync() doesn't guarantee durability past
   power failures. fcntl(F_FULLFSYNC) is required for that purpose."
   `[LDB-SRC:util/env_posix.cc]`
3. **fsyncgate (2018).** PostgreSQL discovered that on Linux < 4.13 "fsync() errors can be lost
   in various ways; also buffers are marked clean after errors, so retrying fsync() can falsely
   report success". Even on newer kernels "you still only get the error once (so retrying
   fsync() is not OK)". PostgreSQL's fix: "PostgreSQL will now PANIC on fsync() failure."
   `[PG-FSYNC]`

> **Systems-engineer reflex #3:** an `fsync` failure is not a "retry later" error. Treat it as
> "the state of this file is now unknown" — stop, and recover from the log.

## 10. Bits rot: integrity failures and checksums

OSTEP ch. 45 `[OSTEP-45]` lists failures a storage system must expect from a *working* disk:

| Failure | What happens | Detectable by a checksum? |
|---|---|---|
| **Latent sector error** | a sector becomes unreadable; the disk returns an error | yes — and the disk tells you |
| **Block corruption** | the disk returns *wrong bytes* without error (e.g. bus fault, firmware bug) | yes |
| **Torn write** (§4) | part of a multi-sector write landed | yes |
| **Misdirected write** | "write[s] the data to disk correctly, except in the wrong location" | only if the checksum also covers *where* the block belongs |
| **Lost write** | "the device informs the upper layer that a write has completed but in fact it never is persisted" | **no** — "the old block likely has a matching checksum" |

A **checksum** is a small value computed from data; store it next to the data, recompute on
read, compare `[OSTEP-45]`. LevelDB uses **CRC32C** on every WAL record and every SSTable block
`[LDB-LOG]`, `[LDB-TABLE]`; RocksDB says "These checksums are for each SST file block …
A block, once written to storage, is never modified." `[ROCKS-OVERVIEW]`.

> Checksums detect **accidents**. They are not a defence against an **attacker** who can write
> the file — anyone can recompute a CRC (no secret key is involved) **(derived)**. Part 7 picks
> this up.

## 11. Summary

| Fact | Consequence for an LSM-tree |
|---|---|
| Sequential HDD I/O is ~200–300× faster than random `[OSTEP-37]` | turn random writes into sequential appends |
| Flash can't overwrite in place; FTL is log-structured `[OSTEP-44]` | write large immutable files; avoid in-place updates |
| Only 512-byte sector writes are atomic `[OSTEP-37]` | never overwrite live data; checksum everything |
| `write()` returns before data is durable `[MAN-write]` | a WAL + explicit `fsync` decides what "committed" means |
| `rename()` is atomic `[MAN-rename]`; directory needs its own fsync `[MAN-fsync]` | publish new state by write → fsync → rename → fsync dir |
| OS works in 4 KiB pages; CPU in 64 B lines **(measured)** | SSTable blocks ≈ 4 KiB; keep hot in-memory metadata compact |
| fsync can fail/lie `[PG-FSYNC]`, `[OSTEP-42]` | fail-stop on sync errors; verify on recovery |

## Drills

1. **Feel the gap.** Write a C++ program that writes 64 MiB to a file (a) as 16 384 sequential
   4 KiB `pwrite`s, (b) as 16 384 `pwrite`s to *random* 4 KiB-aligned offsets. Time both, first
   without `fsync`, then calling `fdatasync` after every write. Explain the four numbers using
   §4–§6. (Your VM's virtual disk will distort numbers — explaining *why* is part of the drill.)
2. **Torn-write arithmetic.** A 16 KiB node is overwritten in place on a 512-byte-sector disk.
   How many distinct "half-old/half-new" states can a power cut leave? (Hint: which
   *prefixes* of sectors can have landed, assuming in-order writes? Then drop the assumption.)
3. **Read the man pages** for `write(2)`, `fsync(2)`, `rename(2)` in full. Write down one sentence
   from each that surprised you.
4. **Durability ladder.** For each of `fwrite`, `fflush`, `write`, `fdatasync`, `fsync`,
   `fsync(dir)`, say which layer of the §6 diagram the data has reached when the call returns.

## My summary

_(write 5 lines here, in your own words)_
