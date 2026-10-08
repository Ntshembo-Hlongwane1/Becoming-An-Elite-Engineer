# 2.4 — Strict Aliasing and Type Punning

"Type punning" = reading the bytes of one type as another (e.g. a `float`'s bits as a `uint32_t`).
It's everywhere in systems code: serialization, hashing, network headers, tagged unions. Done the
wrong way it is **undefined behaviour that the optimiser actively exploits**, and the resulting bug
changes with `-O` level — the nastiest kind to debug.

## 1. The strict-aliasing rule

The compiler assumes that two pointers of *unrelated* types never refer to the same object. That
assumption lets it keep a value in a register across a write through another type. The rule that
makes the assumption legal, cppreference `[CPPREF-reinterpret]`:

> an object of dynamic type `T_obj` is *type-accessible* through a glvalue of type `T_ref` if `T_ref`
> is similar to:
> - "`char`, `unsigned char` or `std::byte`: this permits examination of the object representation of
>   any object as an array of bytes."
> - "`T_obj`" (its own type)
> - "the signed or unsigned type corresponding to `T_obj`"
>
> "If a program attempts to read or modify the stored value of an object through a glvalue through
> which it is not type-accessible, the behavior is undefined."

So you may read a `T` through: a `T*`, a `const T*`, its signed/unsigned twin, or any
`char*`/`unsigned char*`/`std::byte*`. **You may not** read an `int` through a `float*`, or a
`Foo` through a `Bar*` — even though `reinterpret_cast` lets you write it.

## 2. What it does when you break it — measured

```cpp
int punned(int* pi, float* pf) {
    *pi = 1;            // write an int
    *pf = 2.0f;         // write a float to (aliased) storage
    return *pi;         // read the int back
}
```

`float*` is not type-accessible for an `int`, so the compiler assumes `*pf = 2.0f` can't have changed
`*pi` and may return the cached `1`. **(measured)**:

```
result(-O2): 1              ← compiler kept the stale int
result(-O0): 1073741824     ← 0x40000000 = the bits of 2.0f; it actually reloaded
```

Same program, two answers, chosen by the optimiser. That's UB. "It worked in a debug build" is how
this ships to production and then breaks.

Also note: even before aliasing, `reinterpret_cast<float*>(&x)` **doesn't create a `float`** —
`[CPPREF-reinterpret]`: "No temporary is materialized ... no copy is made, no constructors ... are
called." It just relabels the pointer. There is still an `int` in that storage, so reading `*pf` is
UB on two counts (wrong type, and no `float` object exists). And it ignores alignment (alignment
lesson Part 1 §12).

## 3. The right way: `std::memcpy` and `std::bit_cast`

To turn bytes into a value (or back), **copy the bytes into a real object of the target type.** That
is always legal because the destination genuinely is a `T`, and `memcpy` reads the source as bytes
(the `unsigned char` exception). **(measured)** — both give the correct `0x40000000`:

```cpp
float f = 2.0f;
std::uint32_t u;
std::memcpy(&u, &f, sizeof u);                 // u now holds f's bit pattern  (C++11, always OK)
auto v = std::bit_cast<std::uint32_t>(f);      // same, one expression, constexpr  (C++20)
```

Rules:
- Both require the two types to be the **same size** (`bit_cast` enforces it at compile time) and the
  target to be **trivially copyable** (Lesson 2.1 §4).
- `memcpy` is the portable floor (works back to C++98); `std::bit_cast` is the modern form and is
  `constexpr` `[CPPREF-bit_cast]`. Compilers turn both into zero instructions at `-O2` — you pay
  nothing for correctness.
- Neither touches **endianness** (Lesson 1.5): the bytes are taken as-is. For a portable *format*,
  use the shift codec (Lesson 1.5), not a raw `bit_cast`.

## 4. The one real exception: viewing storage as bytes

Going the other direction — treating any object as an array of bytes — **is** allowed, through
`unsigned char*`/`std::byte*` (the first bullet in §1). That's what lets you hash an object, write it
to disk, or implement `memcpy`. It is the foundation of the alignment lesson's buffers and of this
lesson's `ByteCursor`. The asymmetry is the whole point: **bytes-from-object is fine; object-from-
bytes needs a copy into a real object.**

## 5. `std::launder` and placement new (preview)

When you construct a new object into existing storage (placement new, Lesson 8), the compiler may
still hold a pointer that "knows" the old object was there. `std::launder(p)` (C++17) tells the
compiler "re-fetch what's actually at this address" — it's the escape hatch for a few lifetime/reuse
corner cases. You rarely need it if you use the pointer returned by placement-new directly; Lesson 8
shows the exact cases. Mentioned here so the word isn't a surprise.

## 6. Why a researcher cares (bridge to §2.6)
Strict-aliasing violations are **type confusion** in slow motion: the program treats one type's bytes
as another type's object. When the "type" includes a vtable pointer or a length field, an attacker
who controls the bytes controls a pointer or a size — the root of many RCEs. The defence is exactly
§3: never reinterpret; copy into the real type, validate, then use.

## Drills
1. Reproduce the §2 miscompile at `-O0` and `-O2`. Then rewrite `punned` with `memcpy` and show both
   optimisation levels now agree.
2. `static_assert(sizeof(float)==sizeof(std::uint32_t))`, then round-trip a float through
   `bit_cast` to `uint32_t` and back; confirm equality. Try `bit_cast` between different sizes and
   read the compile error.
3. Write `template<class T> std::array<std::byte,sizeof(T)> to_bytes(const T&)` using `memcpy`.
   Which `static_assert` must it carry? (Lesson 2.1 §4.)
4. Take an IP-header-like `struct` (see `[HPC]`'s raw-packet structs) and explain why reading fields
   by `reinterpret_cast<Header*>(packet_bytes)` is UB on two counts, and how `memcpy` into a
   `Header` plus the Lesson-1 codec fixes both.

## My summary
