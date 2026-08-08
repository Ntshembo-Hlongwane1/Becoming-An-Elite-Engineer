# 03 — DiskManager I: Raw I/O

> **Build target:** `internal/kernal/core/storage/DiskManager.hpp` and `.cpp` — open a file,
> read page N, write page N, force it to the platter. Roughly 150 lines. At the end you will
> write a page, kill the process, restart, and read it back.
>
> **The layer's one job:** turn a `page_id_t` into a byte offset and move 4096 bytes across
> the user/kernel boundary. It does not know what a B+Tree is. It does not cache. It does not
> allocate (that's doc 04). If you find yourself wanting to add intelligence here, that
> intelligence belongs a layer up.

---

## 1. The interface, decided before the implementation

```cpp
class DiskManager {
public:
    explicit DiskManager(const std::string& path);
    ~DiskManager();

    void ReadPage (page_id_t pageId, Page& out);
    void WritePage(page_id_t pageId, const Page& in);
    void Sync();                          // durability barrier

    std::size_t NumPages() const;         // file size / PAGE_SIZE
};
```

Note what is absent. No `GetPage` returning a pointer — that would imply caching and ownership
we do not have. No `Allocate` — doc 04. No `page_id_t` validation against a free list — doc 04.
Four methods, each of which maps to one or two syscalls.

Note also that `ReadPage` takes `Page&` rather than returning one. A `Page` is 4 KB; returning
by value invites a copy the optimiser may or may not elide, and in the hot path of a storage
engine "may or may not" is not good enough. **The caller owns the buffer, we fill it.** This is
the standard shape for I/O APIs at every level, from `read(2)` upward, and now you know why.

---

## 2. Why `FILE*` and not `std::fstream`

This will look like a step backwards. It is not, and the reason is concrete rather than
aesthetic.

**`std::fstream` gives you no way to reach the underlying file descriptor.** There is no
portable `.fd()`, no `.native_handle()`. And without the descriptor you cannot call `fsync`
(POSIX) or `_commit` (Windows), which means **you cannot implement durability at all**.
`fstream::flush()` sounds like it does this. It does not — §7 is entirely about that
distinction, and it is the most consequential misunderstanding in this doc.

`std::fopen` gives you a `FILE*`, and `fileno()` / `_fileno()` extracts the descriptor. That
single fact decides the API.

Two secondary benefits worth having:

- **`setvbuf` lets us disable stdio's internal buffer.** We are building a buffer pool in doc
  06 — its entire purpose is to be the cache. Letting stdio *also* buffer means every read is
  copied twice (kernel → stdio buffer → your `Page`) and cached twice, in memory you did not
  budget for. Turning it off removes one full 4 KB `memcpy` from every single I/O.
- **It is one step closer to doc 12**, where `fread`/`fseek` become `pread`/`pwrite` and the
  `FILE*` becomes a bare descriptor.

---

## 3. Opening the file — three traps in five lines

```cpp
DiskManager::DiskManager(const std::string& path) : m_Path(path) {
    // Trap 1: "r+b" requires the file to already exist; it will not create one.
    //         "w+b" creates -- but TRUNCATES an existing file to zero bytes, which on a
    //         database file means deleting the user's entire index on open.
    // So: try to open an existing file first, and only create if that failed.
    m_File = std::fopen(path.c_str(), "r+b");

    if (m_File == nullptr) {
        // Didn't exist (or wasn't readable). Create it, then reopen in update mode so the
        // rest of the class only ever deals with one mode.
        m_File = std::fopen(path.c_str(), "w+b");
        if (m_File == nullptr) {
            throw std::runtime_error("DiskManager: cannot open or create " + path);
        }
    }

    // Trap 3: stdio buffers by default. We are the cache. Double-buffering costs an extra
    // 4 KB memcpy per I/O and a second copy of every hot page in memory we didn't account for.
    std::setvbuf(m_File, nullptr, _IONBF, 0);
}
```

### Trap 1 — the create-vs-truncate dance

There is no single `fopen` mode meaning "open for read/write, create if absent, preserve if
present". `"w+b"` truncates; `"a+b"` forces all writes to the end regardless of your seek
(fatal for random-access page writes); `"r+b"` won't create. The two-step above is the
standard workaround and every storage engine has some version of it.

> There is a race here: between the failed `"r+b"` and the `"w+b"`, another process could
> create the file, and we would truncate it. Single-process engine, so it does not matter to
> us — but notice it, because it is exactly the kind of TOCTOU window that becomes a real bug
> the day someone runs two instances. The fix is `open(2)` with `O_CREAT | O_EXCL`, which doc
> 12 gets for free when we move to descriptors.

### Trap 2 — the `b` in `"r+b"` is not optional on Windows

**This is the one that will actually bite you**, because you are on Windows and it fails
silently rather than loudly.

In text mode, the C runtime translates bytes on the way through:

- Writing `0x0A` (`\n`) emits **two** bytes, `0x0D 0x0A`.
- Reading `0x0D 0x0A` returns **one** byte.
- A `0x1A` byte (Ctrl-Z) is treated as end-of-file.

Your pages are binary. They contain `0x0A` constantly — it is just the number 10, which
appears in keys, counts, page ids, everything. In text mode, a page containing three `0x0A`
bytes writes as 4099 bytes, every subsequent page offset is wrong, and the file is
irrecoverably corrupt. The failure appears far from the cause and looks like random garbage.

On Linux this bug is invisible because text and binary modes are identical there. Which means
**a Windows developer who forgets `b` sees corruption, and a Linux developer who forgets it
ships a landmine to Windows users.** Always write `b`.

### Trap 3 — `_IONBF` and the double-buffer

`setvbuf(file, nullptr, _IONBF, 0)` disables stdio's internal buffer, so `fread` becomes
approximately a direct `read` syscall.

Is this right? For this engine, yes — we cache 4 KB-aligned pages ourselves and never do small
reads. stdio's buffer exists to amortise syscalls across many small `fgetc`-style calls; we
have none. It would only add a copy.

It is worth knowing the general principle, though, because it generalises far beyond here:
**buffering is only a win at the layer that knows the access pattern.** Ours does; stdio's
does not. Redundant buffering at multiple layers is one of the most common sources of
invisible overhead in a system, and one of the easier wins when you go looking for latency.

---

## 4. The offset arithmetic

```cpp
static std::int64_t OffsetOf(page_id_t pageId) {
    return static_cast<std::int64_t>(pageId) * static_cast<std::int64_t>(PAGE_SIZE);
}
```

Three lines of thought hiding in one line of code.

**Why the cast comes first.** `pageId` is `uint32_t`. `pageId * PAGE_SIZE` would compute in
32-bit (or in `size_t`, depending on the platform's `PAGE_SIZE` type) and **overflow at page
1,048,576 — a mere 4 GB into the file.** Widening *before* multiplying is the fix, and putting
the widening in one named function means you cannot forget it at one of the several call sites.

**Why `int64_t` and not `uint64_t`.** The seek APIs take signed offsets (`off_t`, `_fseeki64`),
because they also support seeking backwards from the end. Matching the API's type avoids an
implicit conversion at the call boundary.

> **This is the archetypal low-level bug.** It is silent, it is correct for every small test,
> and it detonates only once your file crosses 4 GB — by which time you have real data in it.
> The habit to build: **whenever you multiply an index by a size, ask what type the
> multiplication happens in.** It is almost never the type you wanted.

---

## 5. `ReadPage` — and the short-read problem

```cpp
void DiskManager::ReadPage(page_id_t pageId, Page& out) {
    if (pageId == INVALID_PAGE_ID) {
        throw std::runtime_error("DiskManager::ReadPage: INVALID_PAGE_ID");
    }

    const std::int64_t offset = OffsetOf(pageId);

    if (Seek(offset) != 0) {
        throw std::runtime_error("DiskManager::ReadPage: seek failed");
    }

    const std::size_t got = std::fread(out.data, 1, PAGE_SIZE, m_File);

    if (got == PAGE_SIZE) {
        return;                                     // the common case
    }

    // Short read. Two possible causes, and they need opposite responses.
    if (std::ferror(m_File)) {
        std::clearerr(m_File);
        throw std::runtime_error("DiskManager::ReadPage: I/O error on page "
                                 + std::to_string(pageId));
    }

    // Not an error: we read past end-of-file. This is legitimate and expected -- see below.
    // Zero-fill the remainder so the caller never sees uninitialised memory.
    std::clearerr(m_File);
    std::memset(out.data + got, 0, PAGE_SIZE - got);
}
```

### Why reading past EOF is normal, not a bug

You will hit this in doc 04 the first time you allocate a page. The sequence is:

1. `AllocatePage()` returns id 7 because the file is 7 pages long.
2. The buffer pool hands the caller a blank page 7 to fill in.
3. Nothing has been *written* to page 7 yet — the file is still 7 pages (0–6) long.
4. Something evicts and re-fetches page 7 before it was ever flushed.
5. `ReadPage(7)` seeks to offset 28672, which is the exact end of the file, and reads 0 bytes.

That is not corruption; it is "this page exists logically but has no bytes on disk yet". The
correct response is a page of zeros, which is what a freshly allocated page should look like
anyway. Throwing here would break allocation for no reason.

### Why zeroing matters more than it looks

If you skipped the `memset`, the caller would receive whatever was in `out.data` before — the
contents of some *unrelated* page that previously occupied that buffer. Doc 05's
`NodePage` view would read a stale `keyCount` and happily walk through another node's keys.
That is not a crash; it is a **silent wrong answer**, the worst possible failure mode.

> **The general habit: never hand back a partially-filled buffer.** Either fill it completely,
> or fail. "Partially valid" is a state your callers cannot reason about, and every one of
> them will forget to check.

### Why `ferror` and not the return value alone

`fread` returning less than requested means *either* EOF *or* error, and the return value
alone cannot distinguish them. `ferror` is the discriminator. `clearerr` is needed because
both the error and EOF flags are **sticky**: leave them set, and every subsequent `fread` on
this handle fails or returns 0 regardless of what you asked for. Forgetting `clearerr` produces
a storage engine that works until the first read past EOF and then never reads anything again.

---

## 6. `WritePage`

```cpp
void DiskManager::WritePage(page_id_t pageId, const Page& in) {
    if (pageId == INVALID_PAGE_ID) {
        throw std::runtime_error("DiskManager::WritePage: INVALID_PAGE_ID");
    }

    const std::int64_t offset = OffsetOf(pageId);

    if (Seek(offset) != 0) {
        throw std::runtime_error("DiskManager::WritePage: seek failed");
    }

    const std::size_t put = std::fwrite(in.data, 1, PAGE_SIZE, m_File);
    if (put != PAGE_SIZE) {
        std::clearerr(m_File);
        throw std::runtime_error("DiskManager::WritePage: short write on page "
                                 + std::to_string(pageId));
    }

    // Push the bytes out of any userspace buffer into the kernel. This is NOT durability --
    // see section 7. It exists so that a later ReadPage of the same page sees the new data
    // even if buffering were re-enabled.
    std::fflush(m_File);
}
```

**A short write is always an error**, unlike a short read. There is no benign reason for it —
it means the disk is full, a quota was hit, or the device failed. Never zero-pad and continue.

### Writing past the end extends the file

Seeking to offset 28672 in a 28672-byte file and writing 4096 bytes makes it 32768 bytes.
This is how the file grows, and it is why doc 04's `AllocatePage` needs no special "extend"
call — it hands out an id, and the first write materialises it.

Seeking *beyond* the end and writing creates a **sparse hole**: the skipped region reads as
zeros and (on NTFS and ext4) may consume no physical space. Our allocator never does this
because it hands out ids consecutively, but know the behaviour exists — it is why a corrupt
`numPages` in your header can silently produce a file with holes rather than an error.

---

## 7. `Sync` — the most misunderstood function in storage

```cpp
void DiskManager::Sync() {
    std::fflush(m_File);                  // stdio buffer  -> kernel page cache

#if defined(_WIN32)
    if (_commit(_fileno(m_File)) != 0) {  // kernel page cache -> device
        throw std::runtime_error("DiskManager::Sync: _commit failed");
    }
#else
    if (::fsync(::fileno(m_File)) != 0) {
        throw std::runtime_error("DiskManager::Sync: fsync failed");
    }
#endif
}
```

### The three places your bytes can be

```
   your Page  --fwrite-->  stdio buffer  --fflush-->  kernel page cache  --fsync-->  device
                           [we disabled                [survives process        [survives
                            this in ctor]               crash, NOT power loss]   power loss]
```

This is the distinction that matters, and it is why `Sync` cannot be built on `fstream`:

| Failure | `fflush` alone survives it? | `fsync` survives it? |
|---|---|---|
| Your process crashes / is killed | **Yes** | Yes |
| The OS panics or power is cut | **No** | Yes |

`fflush` only moves bytes from *your* memory to the *kernel's* memory. Both are RAM. If the
machine loses power, both are equally gone. Only `fsync`/`_commit` asks the device to
acknowledge that the data is in non-volatile storage.

### The cost, and why doc 11 cares

From doc 01 §2: `fsync` is **0.5–10 milliseconds**. Your inserts are microseconds. Calling
`Sync()` per insert makes your engine roughly a thousand times slower, and it will look like
"disks are slow" rather than "I made an architectural mistake".

The resolution — which doc 11 §4 builds — is that durability is a *batch* property. You do
work in memory, and `Sync()` at a commit boundary that covers thousands of operations. The
guarantee you offer becomes "everything up to the last checkpoint survives", which is exactly
what real databases offer, and exactly why they have a `COMMIT` statement.

> **A caveat worth knowing exists:** some consumer SSDs lie about `fsync`, acknowledging once
> data is in the drive's own volatile write cache. Enterprise drives have capacitors to flush
> that cache on power loss; consumer drives often do not. You cannot fix this in software.
> Mention it when someone claims a benchmark proves their database is durable.

---

## 8. Errors: throw, don't return

Every failure path above throws. That is a deliberate choice worth stating.

An I/O error at this layer is not something a caller can meaningfully handle. What would
`BPlusTree::Insert` do with "the disk failed"? It cannot retry usefully, it cannot proceed,
and it certainly cannot continue with a half-updated tree. The only correct behaviours are
*abort the operation* and *do not corrupt anything further* — which is precisely what an
exception does, while a returned error code invites being ignored.

The contrast is `ReadPage` past EOF, which we *don't* throw for. That is the test to apply:
**is this a condition the caller could plausibly be in through no fault of their own, as part
of normal operation?** Past-EOF: yes, during allocation. Disk failure: no. First one gets a
defined result, second one throws.

---

## 9. The complete files

```cpp
#pragma once
// internal/kernal/core/storage/DiskManager.hpp
//
// Moves 4 KB pages between a file and caller-owned buffers. See
// lesson doc/storage/03-diskmanager-io.md and 04-diskmanager-allocation.md.
//
// Not thread-safe. Does not cache -- that is BufferPool (doc 06).

#include <cstdio>
#include <cstdint>
#include <string>
#include "Page.hpp"

class DiskManager {
public:
    explicit DiskManager(const std::string& path);
    ~DiskManager();

    DiskManager(const DiskManager&)            = delete;   // owns a FILE*
    DiskManager& operator=(const DiskManager&) = delete;

    void ReadPage (page_id_t pageId, Page& out);
    void WritePage(page_id_t pageId, const Page& in);
    void Sync();

    std::size_t NumPages() const;
    const std::string& Path() const { return m_Path; }

    // Counters -- doc 07 uses these to prove the buffer pool actually avoids I/O.
    std::uint64_t ReadCount()  const { return m_Reads;  }
    std::uint64_t WriteCount() const { return m_Writes; }

private:
    static std::int64_t OffsetOf(page_id_t pageId);
    int  Seek(std::int64_t offset);

    std::string   m_Path;
    std::FILE*    m_File   = nullptr;
    std::uint64_t m_Reads  = 0;
    std::uint64_t m_Writes = 0;
};
```

```cpp
// internal/kernal/core/storage/DiskManager.cpp
#include "DiskManager.hpp"

#include <cstring>
#include <stdexcept>

#if defined(_WIN32)
  #include <io.h>            // _commit, _fileno, _chsize_s
  #define PORTABLE_FSEEK  _fseeki64
  #define PORTABLE_FTELL  _ftelli64
#else
  #include <unistd.h>        // fsync, fileno
  #define PORTABLE_FSEEK  fseeko
  #define PORTABLE_FTELL  ftello
#endif

DiskManager::DiskManager(const std::string& path) : m_Path(path) {
    m_File = std::fopen(path.c_str(), "r+b");        // existing file, do not truncate
    if (m_File == nullptr) {
        m_File = std::fopen(path.c_str(), "w+b");    // create
        if (m_File == nullptr) {
            throw std::runtime_error("DiskManager: cannot open or create " + path);
        }
    }
    std::setvbuf(m_File, nullptr, _IONBF, 0);        // we are the cache; see doc 03 section 3
}

DiskManager::~DiskManager() {
    if (m_File != nullptr) {
        // Best effort. A destructor must not throw: if the process is already unwinding from
        // another exception, throwing here calls std::terminate and you lose the original
        // error -- and the diagnosis with it.
        std::fflush(m_File);
        std::fclose(m_File);
        m_File = nullptr;
    }
}

std::int64_t DiskManager::OffsetOf(page_id_t pageId) {
    // Widen BEFORE multiplying. See doc 03 section 4 -- overflows at 4 GB otherwise.
    return static_cast<std::int64_t>(pageId) * static_cast<std::int64_t>(PAGE_SIZE);
}

int DiskManager::Seek(std::int64_t offset) {
    return PORTABLE_FSEEK(m_File, offset, SEEK_SET);
}

void DiskManager::ReadPage(page_id_t pageId, Page& out) {
    if (pageId == INVALID_PAGE_ID) {
        throw std::runtime_error("DiskManager::ReadPage: INVALID_PAGE_ID");
    }
    if (Seek(OffsetOf(pageId)) != 0) {
        throw std::runtime_error("DiskManager::ReadPage: seek failed on page "
                                 + std::to_string(pageId));
    }

    const std::size_t got = std::fread(out.data, 1, PAGE_SIZE, m_File);
    ++m_Reads;

    if (got == PAGE_SIZE) {
        return;
    }
    if (std::ferror(m_File)) {
        std::clearerr(m_File);
        throw std::runtime_error("DiskManager::ReadPage: I/O error on page "
                                 + std::to_string(pageId));
    }

    // Past EOF: an allocated-but-never-written page. Zero-fill; never expose stale bytes.
    std::clearerr(m_File);
    std::memset(out.data + got, 0, PAGE_SIZE - got);
}

void DiskManager::WritePage(page_id_t pageId, const Page& in) {
    if (pageId == INVALID_PAGE_ID) {
        throw std::runtime_error("DiskManager::WritePage: INVALID_PAGE_ID");
    }
    if (Seek(OffsetOf(pageId)) != 0) {
        throw std::runtime_error("DiskManager::WritePage: seek failed on page "
                                 + std::to_string(pageId));
    }

    const std::size_t put = std::fwrite(in.data, 1, PAGE_SIZE, m_File);
    ++m_Writes;

    if (put != PAGE_SIZE) {
        std::clearerr(m_File);
        throw std::runtime_error("DiskManager::WritePage: short write on page "
                                 + std::to_string(pageId));
    }
    std::fflush(m_File);
}

void DiskManager::Sync() {
    std::fflush(m_File);
#if defined(_WIN32)
    if (_commit(_fileno(m_File)) != 0) {
        throw std::runtime_error("DiskManager::Sync: _commit failed");
    }
#else
    if (::fsync(::fileno(m_File)) != 0) {
        throw std::runtime_error("DiskManager::Sync: fsync failed");
    }
#endif
}

std::size_t DiskManager::NumPages() const {
    if (PORTABLE_FSEEK(m_File, 0, SEEK_END) != 0) {
        throw std::runtime_error("DiskManager::NumPages: seek to end failed");
    }
    const std::int64_t bytes = PORTABLE_FTELL(m_File);
    if (bytes < 0) {
        throw std::runtime_error("DiskManager::NumPages: ftell failed");
    }
    // Integer division truncates. A trailing partial page means a torn write or a crash
    // mid-extend; we round down and treat the partial page as not existing. Doc 11 section 5
    // revisits this as a recovery concern.
    return static_cast<std::size_t>(bytes) / PAGE_SIZE;
}
```

> `NumPages()` is declared `const` but seeks, which mutates the handle's file position. That
> is a real wart — a `const` method with an observable side effect on a subsequent `fread`.
> It is safe here only because every read and write seeks explicitly first. Doc 12 removes the
> wart entirely by moving to `pread`/`pwrite`, which take the offset as an argument and never
> touch the shared position. Notice the smell now so you recognise why that change is an
> improvement and not just a different spelling.

---

## Checkpoint

Write `storage/tests/diskmanager_io_test.cpp`. The last part is the one that matters — it is
the first time your data outlives the process.

```cpp
#include "../DiskManager.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

static void fill(Page& p, std::uint8_t v) { std::memset(p.data, v, PAGE_SIZE); }

int main(int argc, char** argv) {
    const std::string path = "dm_test.db";
    auto page  = std::make_unique<Page>();
    auto check = std::make_unique<Page>();

    // Pass "verify" as argv[1] on the second run to prove the data survived the process.
    if (argc > 1 && std::string(argv[1]) == "verify") {
        DiskManager dm(path);
        dm.ReadPage(3, *check);
        assert(static_cast<std::uint8_t>(check->data[0])           == 0xAB);
        assert(static_cast<std::uint8_t>(check->data[PAGE_SIZE-1]) == 0xAB);
        std::cout << "survived process restart OK\n";
        return 0;
    }

    std::remove(path.c_str());
    {
        DiskManager dm(path);
        assert(dm.NumPages() == 0);

        // Read of a page that does not exist yet -> zeros, not an error, not garbage.
        fill(*check, 0xFF);
        dm.ReadPage(0, *check);
        for (std::size_t i = 0; i < PAGE_SIZE; ++i) assert(check->data[i] == std::byte{0});

        // Round-trip.
        fill(*page, 0xAB);
        dm.WritePage(3, *page);           // writing page 3 extends the file to 4 pages
        assert(dm.NumPages() == 4);

        fill(*check, 0x00);
        dm.ReadPage(3, *check);
        assert(std::memcmp(page->data, check->data, PAGE_SIZE) == 0);

        // The gap pages 0..2 exist and read as zeros.
        dm.ReadPage(1, *check);
        for (std::size_t i = 0; i < PAGE_SIZE; ++i) assert(check->data[i] == std::byte{0});

        // Reopening must NOT truncate.
    }
    {
        DiskManager dm(path);
        assert(dm.NumPages() == 4);       // if this is 0, your fopen mode truncated the file
        dm.ReadPage(3, *check);
        assert(static_cast<std::uint8_t>(check->data[0]) == 0xAB);
        dm.Sync();
    }

    std::cout << "diskmanager_io_test OK -- now run: " << argv[0] << " verify\n";
}
```

Before doc 04, you should have:

- [ ] `DiskManager` compiling with `-Wall -Wextra`, no warnings
- [ ] The test passing, **including the second `verify` run in a separate process**
- [ ] Deliberately broken it once: change `"r+b"` to `"w+b"` and watch `NumPages()` come back
      0 on reopen. That is what "truncated the user's database on startup" looks like.
- [ ] Deliberately broken it a second time: drop the `b` from both modes, rerun, and see
      whether your 0xAB page survives. On Windows it will not.
- [ ] An answer to: *why does `fflush` not make data durable, and what does?*
- [ ] An answer to: *why is a short read acceptable but a short write never?*

Next: [04 — DiskManager II: Allocation](04-diskmanager-allocation.md), where page 0 becomes
the file header and we stop hardcoding page numbers.
