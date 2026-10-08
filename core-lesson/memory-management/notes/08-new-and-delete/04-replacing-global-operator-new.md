# 8.4 — Replacing the global `operator new` / `operator delete`

Because `operator new`/`operator delete` are ordinary **replaceable** functions (§8.2), you may define
your own at global scope and the linker will use yours for the entire program — every `new`, every
container, every library that allocates through the standard path. This is the single most useful hook
in C++ memory tooling: it is how leak detectors, allocation profilers, and (conceptually) ASan's heap
interception attach to a program without changing a line of its code. The exercise *is* this.

## 1. The rule: define them at global scope, matched

cppreference's replacement-function rules `[CPPREF-replacement, CPPREF-opnew]`:
- The global `operator new`/`operator delete` overloads are **replaceable**: "User programs may
  define" them, and a valid replacement **affects the whole program** `[CPPREF-opnew]`.
- Your replacement must have the exact signature of the one it replaces, must be at global scope (not
  in a namespace), and there must be **exactly one** definition in the program (they are *not* `inline`
  — one strong definition, or you get ODR / "multiple definition" link errors).
- Keep the set **matched** (§8.2 §2). The compiler chooses *which* form to call; if you provide `new`
  but not the matching `delete`, or the `new[]` but not `delete[]`, some frees route to your allocator
  and some to the default → mismatch and corruption. A safe minimal set to replace together:

  ```
  operator new(size_t)                       operator delete(void*) noexcept
  operator new[](size_t)                      operator delete[](void*) noexcept
  operator new(size_t, const nothrow_t&)      operator delete(void*, size_t) noexcept   // sized
  operator new[](size_t, const nothrow_t&)    operator delete[](void*, size_t) noexcept // sized
  ```

## 2. Counting needs a size header — hello again, Lesson 7

To count *bytes* (not just calls) you hit the same problem `malloc`/`free` had: `operator delete(void* p)`
is given only a pointer, not a size — so how does it know how many bytes to subtract from the live
total? Two options:
- Use the **sized** `operator delete(void*, size_t)` the compiler often supplies (§8.1 (measured)).
  But it isn't guaranteed for every delete path, so you can't rely on it alone.
- Do exactly what Lesson 7.2 did: **store the size in a small header just before the pointer you
  return.** Over-allocate by `kHeader` bytes, stash the request size (and a magic number) there,
  return `raw + kHeader`; on delete, read the header back to recover the size. The returned pointer
  stays 16-aligned because `kHeader` is 16 and `malloc` is 16-aligned (§8.2 §3, Lesson 7.2).

The header approach is what the exercise uses — it makes the "`operator new` is `malloc` + bookkeeping"
point concrete, and it is literally a miniature of glibc's chunk header. Your counters then are: live
allocations (`++` in new, `--` in delete), total allocations, bytes requested, live bytes (`+= size`
in new, `-= header.size` in delete), and peak live bytes.

## 3. Three pitfalls that make this harder than it looks

1. **Don't call `new` inside your `operator new`.** Your allocator must obtain memory from something
   *below* `new` — `std::malloc` (Lesson 7) — or you get infinite recursion the first time anything
   allocates. Likewise don't use a `std::vector`/`std::map` of live pointers for bookkeeping *inside*
   the operators (its growth calls `operator new` → recursion). Use fixed storage or plain counters.
2. **They run before `main` and after it.** Static-initialisation (e.g. the test runner's own
   registry, iostreams) allocates **before** `main`, through your replaced operators; destruction
   frees **after** `main`. So your counters must be ready with **no dynamic initialisation of their
   own** — use constant-initialised (`constinit`) globals, not an object constructed on first use that
   itself might allocate. (This also means a program always has some *baseline* live allocations from
   the runtime; the exercise's tests measure **deltas** around each operation, not absolute totals.)
3. **Sized vs unsized delete.** Provide both `operator delete(void*)` and the sized
   `operator delete(void*, size_t)`; the compiler prefers the sized one when available (you saw it
   pick `[operator delete sized]` in §8.1). If you only define the unsized one, the sized default is
   still used for some deletes → half your frees aren't counted. Define both; route both through the
   same header logic (ignore the passed-in size, trust your header).

## 4. Interaction with sanitizers (so your exercise stays ASan-clean)

ASan works by intercepting `malloc`/`free` underneath. Since your replaced `operator new` ultimately
calls `std::malloc`, ASan still sees and bounds-checks every allocation — your header lives inside the
`malloc` region, your returned pointer is in-bounds, and freeing via `std::free(header)` returns the
exact `malloc`ed pointer. The one thing you must **not** do is read your header *after* freeing it
(that's a use-after-free ASan will rightly flag) — so this lesson's counter design reads the header
only while the block is still live, and does no double-free detection (that belongs to Lessons 16–17,
where the detector keeps its metadata *out of line*). Build the exercise under `-fsanitize=address,undefined`
and it stays clean.

## 5. What you'll build
The exercise replaces the global family from §1, backs them with `std::malloc` + a 16-byte size
header, and updates `constinit` counters so that at any point the program can report how many
allocations and bytes are live, the running totals, and the peak. That is a real, if minimal, version
of the tool a profiler ships — and the seed of the leak detector in Lesson 16 and the hardened
allocator in Lesson 17.

## Drills
1. Explain precisely why using `std::map<void*,size_t>` *inside* `operator new` to remember sizes
   causes infinite recursion, while a raw array of `size_t` indexed by a counter does not. (§3.1)
2. Replace only `operator new(size_t)` and `operator delete(void*)` (omit the array and sized forms),
   then run a program that does `new int[4]` / `delete[]`. What goes uncounted or crashes, and why?
   (§1, §3.3)
3. Why must the counters be `constinit` rather than, say, a `static` object built on first use? Give
   the before-`main` sequence of events that a lazily-built counter would get wrong. (§3.2)
4. Your replaced `operator delete` reads `header->size` to update live bytes. Construct the exact
   sequence that would make that read a use-after-free, and state the one ordering rule that prevents
   it. (§4)

## My summary
