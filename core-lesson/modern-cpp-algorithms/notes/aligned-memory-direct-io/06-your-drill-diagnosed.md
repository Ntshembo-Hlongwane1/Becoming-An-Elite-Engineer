# Part 6 — Your Drill, Diagnosed Line by Line

> This is a code review of `notes/drills/include/file.{hpp,cpp}` and `notes/drills/drill1.cpp`
> using only what Parts 1–5 taught. **It's a diagnosis, not a fix.** The fix is Exercise 6, and you
> should write it yourself with the Ex 3 and Ex 5 types. Each finding gives the line, what's wrong,
> the evidence, and the part that explains it.

Evidence was gathered with GCC 15.2 `-std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Wconversion`,
ASan, and `strace` **(measured)**.

---

## A. The bug you asked about

### A1. `file.cpp` — `open(..., O_RDWR | O_CREAT | O_DIRECT, 0644)` + `drill1.cpp` — `std::vector<char> buffer(4096, 'x')`

- **What:** the buffer comes from `std::allocator<char>` → `operator new` → `malloc`, which promises
  16-byte alignment (Part 2 §4, §7). It landed 32-aligned. Your disk's direct-I/O rule is 512
  (Part 5 §6).
- **Evidence:** `Write to: (path.txt) failedInvalid argument`; the probe matrix row
  `buf%4096=32 ... -> Invalid argument` (Part 5 §6).
- **Fix direction:** an aligned owner (Ex 3) or `std::vector<char, AlignedAllocator<char, 512>>`
  (Ex 4), with the alignment obtained from `statx` (Ex 5), not hard-coded.

### A2. `drill1.cpp` — `fm.WriteAt(buffer, i * 5);`

- **What:** offsets 0, 5, 10, … violate rule 3 (file offset must be a multiple of 512). This loop
  would fail as soon as A1 is fixed.
- **Also:** it isn't *random* I/O. Consecutive writes are 5 bytes apart and each 4096-byte write
  overlaps the previous one by 4091 bytes. For a sequential-vs-random experiment you want
  `offset = block_index * block_size` with `block_index` drawn from a shuffled permutation of the
  file's blocks **(derived)**.
- **Evidence:** probe row `off=5 -> Invalid argument` (Part 5 §6).

## B. Correctness bugs that exist even without `O_DIRECT`

### B1. `FileManager` is copyable → double `close`

`file.hpp` declares a destructor but no copy/move operations, so the compiler still generates a
copy constructor that copies `fd_`. Two objects, one fd, two `close` calls. **(measured)** with
`FileManager a{p}; FileManager b = a;` under `strace`:

```
openat(AT_FDCWD, "copy_probe.bin", O_RDWR|O_CREAT|O_DIRECT, 0644) = 3
close(3)                                = 0
close(3)                                = -1 EBADF (Bad file descriptor)
```

Here it just failed with `EBADF`. In a multi-threaded program another thread could have received
fd 3 between the two closes, and you'd close *their* file. That's a real class of bug in servers.
Rule of five, Part 4 §3 (4)–(6). Same family as the fd-leak you studied earlier in
`master-file-manager`.

### B2. `WriteAt_` — `lseek` then `write`

Two syscalls sharing the hidden file offset: not thread-safe, and the retry loop silently depends
on the offset advancing. Use `pwrite` with an explicit `offset + done` (Part 5 §13).

### B3. Partial writes under `O_DIRECT`

Your loop handles partial writes (`done += n`), which is good. With `O_DIRECT`, though, `done`
could end up at a non-multiple of 512, and the next call (`data + done`, `len - done`) would
then break rules 1–3 **(derived)**. You need an explicit policy (Exercise 5 asks you to choose
and document one).

### B4. `n == 0` and the stale `errno`

```cpp
if (n == 0){ throw std::runtime_error("Write (" + filePath_ +") returned 0" + strerror(errno)); }
```

`write` returning 0 isn't an error, so `errno` wasn't set by this call. `strerror(errno)` prints
whatever an earlier call left there. Same for `WriteAt`'s `fd_ == -1` branch (no syscall at all).
Rule: **read `errno` only immediately after a call that reported failure** `[MAN-write]`.

### B5. Error messages are glued together

`"... failed" + strerror(errno)` → `failedInvalid argument`. You saw this in your own output. Add
`": "`. That's small, but log lines are what you'll grep during the red-team phase.

## C. API design (Part 4)

| Line | Issue | Better |
|---|---|---|
| `FileManager(std::string& filePath)` | non-const ref: can't pass `"path.txt"` or a temporary | `explicit FileManager(std::string path)` + `std::move`, or `const std::string&` |
| `void Write(std::vector<char>& data)` | ties the API to `vector` (which can't promise alignment); non-const ref for a read-only parameter | `std::span<const char>` (Part 4 §7) |
| `[[nodiscard]] bool OpenFile_(...)` returns `true` or throws | the `bool` carries no information; `isOpened` is unused → **`-Wunused-variable` (measured)** | return `void`, or return the fd |
| `off_t` in `file.hpp` with no `<sys/types.h>` | compiles only because another header pulled it in **(derived)** | include what you use |
| `SecondsSince_` is a member | uses no member state | free function in an anonymous namespace, or `static` |

## D. Measurement hygiene (your drill is a benchmark)

1. **`std::cout << ... << std::endl` inside every write.** 32 768 lines, each `endl` flushing
   stdout. The printing costs more than buffered writes do (~8 µs each, Part 5 §11) and pollutes
   timing. Time the *whole loop* once, outside.
2. **No `fsync`.** Buffered "write time" measures memcpy into the page cache, not storage (Part 5
   §11: 0.139 s loop vs 0.312 s fsync). With `O_DIRECT`, no `fsync` means metadata isn't durable
   (Part 5 §9).
3. **`O_CREAT` without `O_TRUNC`.** A rerun writes over the previous run's 64 MiB file. Block
   allocation behaviour differs between overwriting and extending a file, so two runs aren't the
   same experiment **(derived)**.
4. **Where the file lives.** In `/tmp` (tmpfs) O_DIRECT "works" with anything (Part 5 §6). State
   the filesystem in your results.

## E. What a correct design looks like (shape only)

```
drill1.cpp
  ├─ DirectFile f = DirectFile::Open("drill.bin", O_CREAT|O_TRUNC|O_RDWR, /*direct=*/true);   (Ex 5)
  ├─ auto [mem_align, off_align] = f.alignment();                                          (statx)
  ├─ AlignedBuffer buf = AlignedBuffer::Allocate(block_size, mem_align);                   (Ex 3)
  ├─ sequential: for i: f.WriteAt(buf.span(), i * block_size)
  ├─ random:     for i in shuffled(0..n): f.WriteAt(buf.span(), i * block_size)
  ├─ f.Sync()
  └─ time each phase once; print one line per phase
```

Write it in Exercise 6, then fill in `RESULTS.md`.

## My summary

