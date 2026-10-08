# Part 5 — The I/O Path and `O_DIRECT`

> Parts 1–4 were about memory. This part follows your bytes from `write()` to the disk and shows
> exactly where alignment becomes a hard rule, why it's a rule *there* and not elsewhere, what
> the kernel checks, what it costs, and how production engines live with it.
> Companion reading: `[MEM §18.6]` (DMA), `[ALGO §11.1]` (external memory), `[LSM-P1 §5–9]`
> (page cache, syscalls, fsync).

Contents

1. Vocabulary: sector, logical block, filesystem block, page
2. The normal path: `write()` copies into the page cache
3. DMA: how a device reads your memory
4. The `O_DIRECT` path: no copy, so the device reads *your* buffer
5. The three rules, in the man page's words
6. Asking the kernel for the rules: `statx(STATX_DIOALIGN)` (measured matrix)
7. Files whose length isn't a multiple of the block: the tail
8. Reads with `O_DIRECT`
9. `O_DIRECT` is not durability
10. The `fork()` hazard
11. Performance: measured on your VM
12. Why databases use it anyway (RocksDB, PostgreSQL)
13. Positional I/O: why `pwrite` beats `lseek + write`
14. Drills

---

## 1. Vocabulary

| Term | Meaning | Your VM **(measured)** |
|---|---|---|
| **Sector** / **logical block size** | smallest unit the storage device reads/writes; addressed by number | 512 (`lsblk LOG-SEC`) |
| **Physical block size** | the size the device writes internally (it may emulate 512 on top of 4096) | 512 (`lsblk PHY-SEC`) |
| **Filesystem block** | the unit ext4 allocates file space in | 4096 (`stat -f -c %S .`) |
| **Page** | the unit of virtual memory and of the page cache | 4096 (`getconf PAGESIZE`) |

Four different "block" sizes, and people mix them up constantly. For `O_DIRECT` on Linux ≥ 2.6
the one that usually matters is the **logical block size of the device** `[MAN-open]`. That's why
your answer is 512, not 4096.

## 2. The normal path: `write()` copies into the page cache

Without `O_DIRECT` **(summarising `[LSM-P1 §5–7]`, `[KERNEL-MM]`)**:

```
your buffer (anywhere, any alignment)
   │  write(fd, buf, n)        ← copy (memcpy-like), CPU does it, byte-granular
   ▼
page cache (kernel memory, page-aligned 4096-byte pages, belongs to the file)
   │  later: writeback (seconds), or now if you fsync()
   ▼
device  ← DMA from the *page cache pages*, which are aligned by construction
```

The kernel **copies** your bytes. A copy can start at any address and have any length, so normal
`write()` accepts any alignment. The device only ever DMAs from page cache pages, which the kernel
allocated aligned. This is why you had never seen an alignment error until you added one flag.

`write()` also returns as soon as the copy is done, **long before** the data reaches the disk.
That's the 0.139 s "write loop" vs 0.312 s "fsync" split in §11.

## 3. DMA: how a device reads your memory

**Direct Memory Access**: instead of the CPU copying every byte to the device, the OS tells the
device controller "transfer N bytes between memory address X and disk sector S". The controller
moves the data itself and interrupts the CPU when it's done `[OSTEP-36 §36.5]`. `[MEM §18.6]`
shows the embedded view of the same thing: program a DMA channel with a `source`, a
`destination` and a `control` word.

```cpp
// [MEM §18.6]
DMA_Channel->source = &dataBuffer;
DMA_Channel->destination = &peripheralDataRegister;
DMA_Channel->control = DMA_ENABLE | DMA_SIZE_16;
```

In that embedded snippet `dataBuffer`'s alignment is whatever the hardware's DMA engine requires.
The book doesn't say, and on real microcontrollers it's usually stated in the reference manual.
On a PC, the storage stack has the same constraint: the device transfers whole **logical blocks**,
and its DMA descriptors describe memory in aligned segments. Linux enforces it by checking
alignment *before* building the request **(derived from `[MAN-open]` + `[ROCKS-DIO]`; the exact
check lives in the block layer and the filesystem's direct-I/O code)**.

## 4. The `O_DIRECT` path: no copy

`[MAN-open]`:

> **O_DIRECT** — Try to minimize cache effects of the I/O to and from this file. In general this
> will degrade performance, but it is useful in special situations, such as when applications do
> their own caching. File I/O is done directly to/from user-space buffers.

```
your buffer  ← must satisfy the device's alignment rules
   │  write(fd, buf, n)    ← no copy: the kernel pins your pages and builds a DMA request
   ▼
device  ← DMA directly from YOUR memory, in whole logical blocks
```

Now the alignment requirements apply to **your** buffer, because your buffer *is* the DMA source.
The kernel can't fix a misaligned buffer without copying it, and avoiding the copy is the whole
point of the flag. So it refuses with `EINVAL` (or, on some filesystems, quietly falls back to
buffered I/O, §5).

## 5. The three rules, in the man page's words

`[MAN-open]`, *NOTES → O_DIRECT*:

> "The O_DIRECT flag may impose alignment restrictions on the length and address of user-space
> buffers and the file offset of I/Os. In Linux alignment restrictions vary by filesystem and
> kernel version and might be absent entirely. The handling of misaligned O_DIRECT I/Os also
> varies; they can either fail with EINVAL or fall back to buffered I/O."

> "In Linux 2.4, most filesystems based on block devices require that the file offset and the
> length and memory address of all I/O segments be multiples of the filesystem block size
> (typically 4096 bytes). In Linux 2.6.0, this was relaxed to the logical block size of the block
> device (typically 512 bytes). A block device's logical block size can be determined using the
> ioctl(2) BLKSSZGET operation or from the shell using the command: `blockdev --getss`"

So there are three rules, each "multiple of the DIO alignment":

| # | What | In your drill |
|---|---|---|
| 1 | **buffer address** | `vector<char>` data at `...a020`, which is 32-aligned → **violated** → `EINVAL` |
| 2 | **length** of each I/O | 4096 → OK |
| 3 | **file offset** | sequential: 0, 4096, … → OK. Your "random" loop `i * 5` → **violated** |

RocksDB's wiki states the same thing for production: "the position indicator (offset), #bytes and
the buffer address must be aligned to the logical sector size" `[ROCKS-DIO]`.

Your drill hit rule 1 first. If you had fixed only the buffer, rule 3 would have failed in the
second loop. (Your second loop also called `lseek` and then `write`; see §13.)

## 6. Asking the kernel for the rules: `statx(STATX_DIOALIGN)`

The man page continues `[MAN-open]`: "Since Linux 6.1, O_DIRECT support and alignment restrictions
for a file can be queried using statx(2), using the STATX_DIOALIGN flag." `[MAN-statx]`:

> **stx_dio_mem_align** — The alignment (in bytes) required for user memory buffers for direct I/O
> (O_DIRECT) on this file, or 0 if direct I/O is not supported on this file.
>
> **stx_dio_offset_align** — The alignment (in bytes) required for file offsets and I/O segment
> lengths for direct I/O (O_DIRECT) on this file, or 0 if direct I/O is not supported on this file.
>
> STATX_DIOALIGN ... is supported on block devices since Linux 6.1. The support on regular files
> varies by filesystem; it is supported by ext4, f2fs, and xfs since Linux 6.1.

The memory rule and the offset/length rule are **two separate numbers**. They can differ.

### The probe program (line by line) — **(measured)**

```cpp
#include <fcntl.h>      // open, O_* flags, AT_EMPTY_PATH
#include <sys/stat.h>   // statx, struct statx, STATX_DIOALIGN
#include <unistd.h>     // pwrite, close, unlink
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : ".";
    std::string path = dir + "/dio_probe.bin";
    int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_DIRECT, 0644);       // (1)
    if (fd < 0) { std::printf("open(O_DIRECT): %s\n", std::strerror(errno)); return 0; }

    struct statx sx{};                                                                 // (2)
    if (statx(fd, "", AT_EMPTY_PATH, STATX_DIOALIGN, &sx) == 0                          // (3)
        && (sx.stx_mask & STATX_DIOALIGN))                                             // (4)
        std::printf("statx DIOALIGN: mem_align=%u offset_align=%u\n",
                    sx.stx_dio_mem_align, sx.stx_dio_offset_align);
    else
        std::printf("statx DIOALIGN not reported\n");

    char* base = static_cast<char*>(std::aligned_alloc(4096, 3 * 4096));               // (5)
    std::memset(base, 'x', 3 * 4096);
    struct T { size_t memoff, len; off_t off; } t[] = {                                // (6)
        {0,4096,0},{16,4096,0},{32,4096,0},{256,4096,0},{512,4096,0},{0,4096,5},{0,4096,512},
        {0,100,0},{0,512,0},{0,1000,0},{0,1024,0}};
    for (auto& c : t) {
        ssize_t n = ::pwrite(fd, base + c.memoff, c.len, c.off);                       // (7)
        std::printf("buf%%4096=%-4zu len=%-5zu off=%-4lld -> %s\n", c.memoff, c.len,
                    (long long)c.off, n < 0 ? std::strerror(errno) : ("ok " + std::to_string(n)).c_str());
    }
    std::free(base); ::close(fd); ::unlink(path.c_str());                              // (8)
}
```

1. Open with `O_DIRECT`. `O_TRUNC` makes each run start from an empty file.
2. `struct statx` is the extended stat structure. `{}` zero-initialises it.
3. `statx(dirfd, pathname, flags, mask, buf)`: with `AT_EMPTY_PATH` and an empty pathname, it
   describes the file **`fd` itself** `[MAN-statx]`. `mask` says which fields we want.
4. **Always check `stx_mask`**: the kernel sets the bits for the fields it actually filled. A
   filesystem that doesn't support DIOALIGN leaves the bit clear, and the fields are then
   meaningless `[MAN-statx]`.
5. One 4096-aligned base buffer. Every test case moves *from* it by a known amount, so we
   control the alignment exactly. `aligned_alloc` → `free` (Part 3 §8).
6. Test cases `{memory offset from base, length, file offset}`.
7. `pwrite` = write at an explicit offset (§13). `base + 16` is 16-aligned but not 32-aligned,
   and so on.
8. Clean up: `free` matches `aligned_alloc`, `close` the fd, `unlink` the file.

Output on your **ext4** repo directory:

```
statx DIOALIGN: mem_align=512 offset_align=512
buf%4096=0    len=4096  off=0    -> ok 4096
buf%4096=16   len=4096  off=0    -> Invalid argument      ← rule 1 (address)
buf%4096=32   len=4096  off=0    -> Invalid argument      ← rule 1: this is YOUR drill's case
buf%4096=256  len=4096  off=0    -> Invalid argument      ← rule 1
buf%4096=512  len=4096  off=0    -> ok 4096               ← 512 is enough; 4096 not required
buf%4096=0    len=4096  off=5    -> Invalid argument      ← rule 3 (offset): your i*5 loop
buf%4096=0    len=4096  off=512  -> ok 4096
buf%4096=0    len=100   off=0    -> Invalid argument      ← rule 2 (length)
buf%4096=0    len=512   off=0    -> ok 512
buf%4096=0    len=1000  off=0    -> Invalid argument      ← rule 2
buf%4096=0    len=1024  off=0    -> ok 1024
```

Output on **tmpfs** (`/tmp`, where my scratch space lives):

```
statx DIOALIGN not reported
buf%4096=0    len=4096  off=0    -> ok 4096
buf%4096=16   len=4096  off=0    -> ok 4096
... every case: ok ...
```

Same code, same kernel, different filesystem, completely different behaviour. tmpfs *is* the
page cache (it's RAM-only), so there's no device to DMA to and nothing to align against
**(derived)**. This matches the man page's "might be absent entirely". **A test suite for
O_DIRECT code that runs in `/tmp` tests nothing.** Exercise 5's tests run in the build directory
on purpose.

### What to do when `statx` doesn't report

The man page's fallback order `[MAN-open]`: statx → filesystem-specific ioctl (e.g. XFS) → the
device's logical block size (`BLKSSZGET` / `blockdev --getss`) → assume. Production engines pick
a **conservative constant that covers everything known**: PostgreSQL uses
`#define PG_IO_ALIGN_SIZE 4096` with the comment "4K corresponds to common sector and memory page
size" `[PG-config]`. 4096 is a multiple of 512, so a 4096-aligned buffer satisfies a 512 rule too
**(derived from Part 1 §6: more low zero bits)**. That's why my "use 4096" advice worked even
though it wasn't the real rule.

## 7. Files whose length isn't a multiple of the block: the tail

Rule 2 says each I/O length must be a multiple of 512. So how do you write a 13-byte file with
`O_DIRECT`? **(measured, ext4)**:

```
pwrite 512 padded: 512                 ← write a full block: 13 real bytes + 499 zero bytes
size after padded write: 512           ← the file is now 512 bytes long (too long!)
ftruncate(12): 0                       ← cut the file back to its logical length
size now: 12
```

The pattern is:

1. Keep the last partial block in an aligned buffer.
2. Pad it to a full block **with zeros** (never with leftover memory; Part 7 §4).
3. `pwrite` the full block at an aligned offset.
4. `ftruncate(fd, logical_size)` to remove the padding from the file length `[MAN-ftruncate]`.
5. When more data arrives, **rewrite that same tail block** (now with more real bytes) at the
   same offset, and truncate again.

```
rewrite block 0 with 17 bytes padded: 512
final size 17
00000000: 6865 6c6c 6f2c 2031 3320 420a 6d6f 7265  hello, 13 B.more
00000010: 0a                                       .
```

This is exactly what RocksDB does. From `WritableFileWriter::WriteDirect`
`[ROCKS-SRC:writable_file_writer.cc]`:

```cpp
// Calculate whole page final file advance if all writes succeed
const size_t file_advance = TruncateToPageBoundary(alignment, buf_.CurrentSize());  // (1)
// Calculate the leftover tail, we write it here padded with zeros BUT we
// will write it again in the future either on Close() OR when the current
// whole page fills out.
const size_t leftover_tail = buf_.CurrentSize() - file_advance;                     // (2)
// Round up and pad
buf_.PadToAlignmentWith(0);                                                         // (3)
...
buf_.RefitTail(file_advance, leftover_tail);                                        // (4)
next_write_offset_ += file_advance;                                                 // (5)
```

1. Align-down (Part 1 §8): how many bytes are in *complete* blocks.
2. The partial block's real byte count.
3. Pad the buffer up to a block boundary with zeros.
4. After writing, move the tail bytes to the front of the buffer so they get re-sent next time.
5. Advance the file offset by **only** the complete blocks, so the next write starts at the
   tail's block again.

And on close, RocksDB's `PosixWritableFile::Close` calls `ftruncate(fd_, filesize_)` to "trim the
extra space" `[ROCKS-SRC:io_posix.cc]`. That's step 4 of the pattern. **Exercise 5's
`DirectAppender` is this design.**

## 8. Reads with `O_DIRECT` **(measured)**

```
O_DIRECT pread(4096 @0) on 12-byte file -> 12, first bytes: hello, 13 B
pread past EOF -> 0
pread len=100 -> -1 Invalid argument
```

- The *request* length must be aligned (100 → `EINVAL`), but the *result* can be short at end of
  file: you asked for 4096 and got 12. The kernel knows the file's real size.
- Past EOF you get 0. Normal EOF semantics.

RocksDB's direct read loop relies on this: if a read returns a non-aligned count, "Bytes reads
don't fill sectors. Should only happen at the end of the file." and it stops `[ROCKS-SRC:io_posix.cc]`.

Newer kernels can report a separate, smaller **read** offset alignment
(`STATX_DIO_READ_ALIGN`, since 6.14, xfs) `[MAN-statx]`. Treat it as an optimisation and don't
rely on it.

## 9. `O_DIRECT` is not durability

`[MAN-open]`:

> "The O_DIRECT flag on its own makes an effort to transfer data synchronously, but does not give
> the guarantees of the O_SYNC flag that data and necessary metadata are transferred. To guarantee
> synchronous I/O, O_SYNC must be used in addition to O_DIRECT."

Bypassing the *page cache* doesn't bypass the **device's own volatile write cache**, and it
doesn't persist the **metadata** (for example the new file size after an append). After a crash
you could have the data blocks but a file length that doesn't include them **(derived)**. For
durability you still need `fdatasync`/`fsync`, or `O_DSYNC`/`O_SYNC` `[MAN-fsync]`,
`[LWN-DURABLE]`, `[LSM-P1 §8]`. In §11 the O_DIRECT `fsync` took ~0.01 s, not 0: there was still
something to flush.

## 10. The `fork()` hazard

`[MAN-open]`:

> "O_DIRECT I/Os should never be run concurrently with the fork(2) system call, if the memory
> buffer is a private mapping (i.e., any mapping created with the mmap(2) MAP_PRIVATE flag; this
> includes memory allocated on the heap and statically allocated buffers). Any such I/Os, whether
> submitted via an asynchronous I/O interface or from another thread in the process, should be
> completed before fork(2) is called. Failure to do so can result in data corruption and undefined
> behavior in parent and child processes."

Why **(derived from Part 3 §6 + §4 above)**: `fork` makes private pages copy-on-write. If the
device is DMA-ing into a page while `fork` marks it COW, the parent and child can end up with
different views of which physical page received the data. The fixes the man page lists: buffers
from `mmap(MAP_SHARED)` / `shmat`, or `madvise(MADV_DONTFORK)` on the buffer `[MAN-madvise]`.
This matters for servers that fork worker processes. Part 7 §6 looks at it as an attacker.

## 11. Performance: measured on your VM

Same total (64 MiB, the size of your drill's loop), `pwrite` from a 4096-aligned buffer, then one
`fsync` **(measured, ext4 on a VirtualBox virtual disk)**:

```
buffered   bs=4096    write-loop=0.139s fsync=0.312s total=0.451s (142 MiB/s)
O_DIRECT   bs=4096    write-loop=9.423s fsync=0.008s total=9.431s (7 MiB/s)
buffered   bs=65536   write-loop=0.028s fsync=0.268s total=0.296s (217 MiB/s)
O_DIRECT   bs=65536   write-loop=0.669s fsync=0.007s total=0.676s (95 MiB/s)
buffered   bs=1048576 write-loop=0.124s fsync=0.350s total=0.474s (135 MiB/s)
O_DIRECT   bs=1048576 write-loop=0.403s fsync=0.011s total=0.413s (155 MiB/s)
```

Reading it **(derived)**:

- **O_DIRECT with 4 KiB writes is ~20× slower.** Each `pwrite` waits for one device round trip:
  9.423 s / 16384 writes ≈ **575 µs per write**. Buffered writes are memory copies (~8 µs each)
  and the kernel later sends them to the disk in big merged batches.
- **Larger direct writes amortise the round trip.** At 1 MiB per write, O_DIRECT matches or beats
  buffered: no double copy and no page-cache churn.
- **Buffered "write" time is a lie about durability.** 0.139 s of copying, then 0.312 s of actual
  disk work in `fsync`. Without `fsync`, the buffered numbers would look 3× better and prove
  nothing.
- The man page wasn't exaggerating: "In general this will degrade performance" `[MAN-open]`.
  The win only appears with big I/Os, or when *you* do the caching (§12).

Your numbers will vary (virtual disk, host cache). Exercise 6 makes you produce your own table.

## 12. Why databases use it anyway

RocksDB `[ROCKS-DIO]`: direct I/O is for "self-caching applications such as RocksDB", which can
use "their knowledge of data semantics" to cache better than the OS. Without it the same block
sits in RAM twice, once in RocksDB's block cache and once in the page cache. It's opt-in
(`use_direct_reads`, `use_direct_io_for_flush_and_compaction`), and RocksDB doesn't use it for
the WAL/MANIFEST.

PostgreSQL added the machinery with commit `faeedbcef` ("Introduce PG_IO_ALIGN_SIZE and align all
I/O buffers"): page buffers on the stack become `PGIOAlignedBlock` (an `alignas` struct, Part 3
§2), heap buffers come from aligned allocation functions, and the shared buffer pool is aligned
`[PG-c.h]`, `[PG-config]`, `[PG-COMMIT]`. The commit message says outright: "There is no standard governing O_DIRECT's requirements". Direct I/O itself is still a developer option (`debug_io_direct`).

The pattern in both:

1. **One alignment constant**, conservative (4096), used everywhere.
2. **Aligned allocation centralised** in one buffer type (RocksDB `AlignedBuffer`, PG
   `PGIOAlignedBlock` / `palloc_aligned`).
3. **Assertions at the syscall boundary** (RocksDB `assert(IsSectorAligned(...))`).
4. **Large I/Os** (RocksDB recommends ≥ 2 MB compaction readahead with direct I/O `[ROCKS-DIO]`).
5. **Off by default.** The man page agrees: "treat use of O_DIRECT as a performance option which
   is disabled by default" `[MAN-open]`.

## 13. Positional I/O: why `pwrite` beats `lseek + write`

Your drill's `WriteAt_` does:

```cpp
::lseek(fd_, offset, SEEK_SET);
::write(fd_, data + done, len - done);
```

`pwrite(fd, buf, n, offset)` does both in one call and "does not change the file offset"
`[MAN-pwrite]`. Three reasons it's better **(derived)**:

1. **Threads:** the file offset is shared by everyone using the fd. Two threads doing
   `lseek`+`write` can interleave (A seeks, B seeks, A writes at B's position). `pwrite` has no
   shared state.
2. **One syscall instead of two.**
3. **Retry loops stay correct:** after a partial write you call `pwrite(fd, buf + done, n - done,
   offset + done)`, which is explicit. With `lseek`+`write` the loop depends on the hidden offset
   having advanced.

With `O_DIRECT` there's a fourth: a partial write of an aligned request could leave `done` at a
non-aligned value, and the retry would then be misaligned. You must handle that case explicitly
(Exercise 5 asks you to decide what to do and document it).

## 14. Drills

1. Run the §6 probe on: your repo directory, `/tmp`, and `/dev/shm`. Make a table. Which one would
   you run O_DIRECT tests on, and why?
2. Run `blockdev --getss /dev/sda` (may need sudo) and compare with `statx`. If they disagree,
   which one wins?
3. Modify the probe so that the memory is 512-aligned but the length is 4096 + 512. Predict, then
   run.
4. Reproduce §7 by hand. Then write 3 bytes, close, reopen *without* `O_DIRECT`, and `xxd` the
   file. Where did the 509 padding bytes go?
5. Re-run the §11 benchmark with `O_DIRECT | O_DSYNC`. Explain the change in the `fsync` column.
6. Explain in 5 sentences why RocksDB keeps the WAL on buffered I/O even when data files use
   O_DIRECT. (Hint: §11 first bullet, and how small WAL appends are.)

## My summary

