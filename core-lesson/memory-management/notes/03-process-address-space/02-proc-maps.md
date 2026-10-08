# 3.2 — Reading `/proc/self/maps`

The segments of §3.1 are an abstraction; `/proc/<pid>/maps` is the kernel's ground-truth list of
every **VMA** (virtual memory area — one contiguous mapping with uniform permissions). Reading it
fluently is a core systems/RE skill, and your exercise parses it.

## 1. The format

`[MAN-proc-maps]` (`man proc_pid_maps`): "The format of the file is:"

```
address           perms offset  dev   inode      pathname
00400000-00452000 r-xp  00000000 08:02 173521    /usr/bin/dbus-daemon
```

Field by field:
- **address** — `start-end` in hex, half-open `[start, end)`. `end - start` is the size in bytes
  (always a multiple of the page size, 4096 — Lesson 4).
- **perms** — four chars `[MAN-proc-maps]`: `r` read, `w` write, `x` execute, then `s` shared or
  `p` "private (copy on write)". So `r-xp` = read+execute, private; `rw-p` = read+write, private.
- **offset** — if the mapping is backed by a file, the byte offset into that file where it starts.
- **dev** — `major:minor` of the device holding the file (`08:02` here); `00:00` for anonymous.
- **inode** — the file's inode; **`0` means no file** — anonymous memory (heap, stack, bss, `mmap`
  of anonymous): `[MAN-proc-maps]` "0 indicates that no inode is associated ... as would be the case
  with BSS".
- **pathname** — the backing file, or a pseudo-path in brackets, or empty (anonymous).

## 2. A real map, annotated (measured on your VM)

```
60f0faa4d000-60f0fab3f000 r--p ... /usr/lib/cargo/bin/coreutils/cat   ┐ the program itself,
60f0fab3f000-60f0faff7000 r-xp ... /usr/lib/cargo/bin/coreutils/cat   │ loaded as several VMAs:
60f0faff7000-60f0fb3b6000 r--p ... /usr/lib/cargo/bin/coreutils/cat   │ r-- (rodata), r-x (code),
60f0fb51b000-60f0fb521000 rw-p ... /usr/lib/cargo/bin/coreutils/cat   ┘ rw- (data) — note W^X
60f12494f000-60f124991000 rw-p 0 0 [heap]                              the heap
7b21d2c00000-7b21d2c28000 r--p ... /usr/lib/.../libc.so.6             ┐ a shared library, same
7b21d2c28000-7b21d2dc0000 r-xp ... /usr/lib/.../libc.so.6             ┘ r--/r-x/rw- split
7b21d3082000-7b21d3083000 ---p 0 0                                     a guard: no permissions
7b21d3087000-7b21d308b000 r--p 0 0 [vvar]                             kernel-exported data
739a2a9e1000-739a2a9e3000 r-xp 0 0 [vdso]                             fast syscalls (vdso(7))
7ffe5ef52000-7ffe5ef74000 rw-p 0 0 [stack]                            the main thread's stack
ffffffffff600000-...      --xp 0 0 [vsyscall]                         legacy syscall page
```

Things to notice (all **measured**):
- **One binary → several VMAs.** The loader maps each ELF segment with its own permissions, so the
  single file `cat` appears as 4–5 lines: `r--` (rodata), `r-x` (code), `rw-` (data). The alignment
  lesson's "code vs data" is literally these separate VMAs.
- **Libraries look the same.** `libc.so.6` has the identical r--/r-x/rw- split, mapped into *your*
  address space (Lesson 4 §… shared pages). This is dynamic linking.
- **`---p` guard regions** have *no* permissions — any access faults. The loader inserts them between
  mappings so an overflow from one region can't silently slip into the next.
- **Pseudo-paths** `[MAN-proc-maps]`: `[heap]`, `[stack]`, `[vdso]` ("virtual dynamically linked
  shared object", fast user-space syscalls), `[vvar]`/`[vvar_vclock]` (kernel data exposed read-only,
  e.g. the clock), `[vsyscall]` (a legacy fixed page). Your parser must recognise the bracketed ones.

## 3. How to classify an address

To answer "what is at address `A`?" you scan the lines for the one VMA with `start ≤ A < end`, then
report its permissions and pathname/pseudo-path. If no line contains `A`, it is **unmapped** —
dereferencing it faults (SIGSEGV). That lookup is exactly:
- what the CPU's page-fault handler does conceptually (Lesson 4),
- what a debugger does to say "0x… is in libc's .text",
- and what **your capstone's shadow-memory check and SIGSEGV handler** build on to turn a raw fault
  address into "heap-use-after-free at block allocated here".

The exercise builds this classifier: parse `maps`, then map any pointer to its region kind
(code / rodata / data-or-bss / heap / stack / library / anonymous-mmap / special / unmapped).

## 4. Caveats that bite parsers
- Lines are **sorted by address** and non-overlapping; you can binary-search once parsed.
- The pathname may contain spaces (rare) or be `(deleted)` for an unlinked file; parse the first five
  whitespace-separated fields, then take the *rest* of the line as the path.
- The map is a **snapshot**; it changes as the program `mmap`s/`munmap`s/grows the heap. Re-read it
  when you need current truth.
- `maps` shows regions, not per-byte state. "Is this exact byte a live object?" needs *your* shadow
  memory (capstone), not `maps`.

## Drills
1. `cat /proc/self/maps` for a tiny program of your own. Identify every line: which are your binary,
   which are libc, which are anonymous, which are pseudo-paths. Find the `---p` guards.
2. Write the five-field parse by hand for one line; extract start, end, perms, and path. Confirm
   `end-start` is a multiple of 4096.
3. Take the measured addresses from §3.1 (`&func`, `&g_init`, heap, `&local`) and, using a maps dump
   from the *same* run, say which VMA each falls in. (Hint: build `-no-pie` and print both.)
4. Why does the heap have `inode 0`? Why does your binary's `.text` line have a real inode and a
   pathname?

## My summary
