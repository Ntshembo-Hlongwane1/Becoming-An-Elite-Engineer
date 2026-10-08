# 2.1 — Objects and Storage Duration

## 1. What "object" means in C++

In everyday speech "object" means "instance of a class". In the C++ standard it means something
more basic: **a region of storage with a type, a value, and a lifetime.** An `int`, a `double`, a
`char[16]`, and a `std::string` are all objects. Functions and references are *not* objects (they
have no storage you can take the size of in the same way).

Each object has:
- a **type** (fixes its size, alignment, and how its bytes are interpreted — Lesson 1),
- an **address** (the byte number of its first byte — Lesson 1 §… / Lesson 3),
- a **value** (the meaningful bits — its *value representation*; the rest is padding),
- a **lifetime** (§3).

## 2. Storage duration: how long the storage lives

Every object gets its storage from one of four **storage durations** `[MEM §2.1]` (the book calls
the first two "static" and the third "dynamic"):

| Duration | Where it lives | Created / destroyed | Example |
|---|---|---|---|
| **static** | the program's data segment (Lesson 3) | at program start / end | globals, `static` locals |
| **thread** | per-thread storage | at thread start / end | `thread_local` |
| **automatic** | the stack (Lesson 5) | on entering / leaving a scope | ordinary locals |
| **dynamic** | the heap (Lessons 7–8) | explicit `new` / `delete`, allocator | `new int`, `std::vector`'s buffer |

`[MEM §2.1]` on the two extremes:

> "Static memory allocation occurs when memory is allocated for variables at compile time ... These
> variables remain in memory throughout the program's execution, and their size cannot be changed or
> freed during runtime." ... "Dynamic Memory Allocation" is allocated at runtime (`new`/`malloc`).

Storage duration decides *when the storage exists*; it does not by itself decide when the *object's*
lifetime runs within that storage — that's §3, and the gap between them is where placement-new
(Lesson 8) and arenas (Lesson 10) live.

## 3. Lifetime: when it is legal to touch an object

Storage existing is not enough; the *object* must be alive. cppreference, summarising the standard
`[CPPREF-lifetime]`:

- **Lifetime begins** when "storage with proper alignment and size is obtained, AND initialization
  is complete."
- **Lifetime ends** when (non-class) "the object is destroyed", (class) "the destructor call
  starts", or when **"the storage is released or reused."**

And the consequence that matters:

> "The following uses of a glvalue ... identifying an out-of-lifetime object are undefined
> behavior: lvalue-to-rvalue conversion [reading its value], access to non-static data members or
> member function calls, ..."

So reading an object before it's initialized, or after it's destroyed / its storage freed, is UB.
That single rule is the whole of Lesson 2.5 (dangling, use-after-free, use-after-scope). Note what
is *still* allowed on the raw storage: you may keep the address around as `void*` and `static_cast`
it back to `char*`/`unsigned char*`/`std::byte*` — you just can't treat it as a live object of the
old type.

### Storage reuse is explicit and legal
`[CPPREF-lifetime]`: "Storage can be reused by explicitly ending an object's lifetime and
constructing a new object in-place." An array of `unsigned char`/`std::byte` "provide[s] storage for
objects if the new object fits entirely within the array." This is the formal basis for byte buffers
holding real objects — the alignment lesson's `AlignedBuffer`, arenas, placement new. We use it fully
in Lesson 8.

## 4. `sizeof`, and trivially-copyable types (needed for the exercise)

- `sizeof(T)` = bytes one `T` occupies, including padding (Lesson 1.1, alignment lesson Part 1 §13).
- A **trivially-copyable** type is one whose object representation can be copied with `std::memcpy`
  and still be a valid value — no custom copy/move/destructor doing bookkeeping. All scalars and
  C-style aggregates of scalars qualify; `std::string` does **not** (it owns a heap pointer). Check
  it with `std::is_trivially_copyable_v<T>` from `<type_traits>`.
- Why it matters here: §2.4 will say the *only* portable way to turn bytes into a `T` (or back) is
  `std::memcpy`/`std::bit_cast`, and those are sound **only for trivially-copyable `T`**. The
  Lesson 2 exercise's `read_object<T>`/`write_object<T>` `static_assert` exactly this.

## Drills
1. Print the storage duration category (in words) for: a global `int`, a `static int` inside a
   function, a plain local, `new int`, a `thread_local int`. For each, say when its storage is
   obtained and released.
2. `static_assert(std::is_trivially_copyable_v<T>)` for `int`, `double`, a POD struct, `std::string`,
   `std::vector<int>`. Which fail and why?
3. Read `[CPPREF-lifetime]`'s "end of lifetime" clause, then predict: is it UB to call a member on an
   object after `delete`? After its scope ends? After its storage is `memcpy`-overwritten by another
   object? (All three are the same rule.)

## My summary
