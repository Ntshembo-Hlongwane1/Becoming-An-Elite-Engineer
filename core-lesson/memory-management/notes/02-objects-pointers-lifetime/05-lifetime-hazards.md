# 2.5 — Lifetime Hazards

Every bug here is one rule from §2.1 §3 broken: **touching an object outside its lifetime.** They
look different in code but are the same defect, and ASan reports each with a precise label.

## 1. Dangling from a returned local (use-after-scope)

```cpp
int* bad() {
    int x = 42;      // automatic storage: lifetime ends when bad() returns
    return &x;       // returns the address of storage that's about to die
}                    // x's lifetime ends here
int main() { int* p = bad(); return *p; }   // use-after-scope: UB
```

The stack slot `x` occupied is reused by the next call; `*p` reads whatever is there now. `[MEM
§1.3.3]` lists dangling pointers as a core hazard. Compilers warn (`-Wreturn-local-addr`), and ASan
(`-fsanitize=address`, with use-after-return detection) reports `stack-use-after-return`. The fix:
return by value, or return storage that outlives the call (heap, or a caller-provided buffer).

## 2. Use-after-free

```cpp
int* p = new int[4];
p[0] = 1;
delete[] p;              // lifetime + storage end here; p now dangles
int z = p[0];            // use-after-free: UB
```

**(measured)**, ASan: `heap-use-after-free`. The read often "works" right after the `delete` because
the bytes linger — until the allocator hands that block to someone else, at which point you read or
corrupt *their* data. That lingering window is precisely what makes UAF exploitable (§2.6). The fix
is ownership (Lessons 11–12): a `unique_ptr` makes the pointer unusable after it's gone.

## 3. Double free

```cpp
int* p = new int(1);
delete p;
delete p;                // double free: UB, corrupts the allocator's bookkeeping
```

`delete` returns the chunk to the allocator's free list (Lesson 7). Doing it twice links a freed
chunk into the free list twice, which an attacker can exploit to make `malloc` later return a pointer
into memory they control (`[MEM §5.1]` heap-corruption family). ASan: `attempt double-free`. The fix
is again ownership: a moved-from `unique_ptr` is null, and `delete nullptr` is a no-op.

## 4. Storage reused out from under a pointer

```cpp
alignas(T) unsigned char buf[sizeof(T)];
new (buf) T{...};                 // a T lives in buf now
// ... later, construct something else there, or let buf's scope end ...
```

If you keep a `T*` into `buf` and then the storage is reused for another object or released, the old
`T*` dangles by §2.1 §3 ("storage is released or reused"). This is the lifetime subtlety behind
placement new and arenas (Lesson 8/10); the rule is identical to §1–§3, just less obvious because no
`delete` appears.

## 5. Dangling views (preview of containers)

A `std::string_view`/`std::span` (or a raw `const char*`) into a container is a pointer; if the
container reallocates (vector growth, Lesson 13) or is destroyed, the view dangles. "Iterator
invalidation" is this same hazard with iterators. Rule from the alignment lesson restated: **a view
may never outlive the storage it views.**

## 6. Why the sanitizers are not optional

None of §1–§4 is guaranteed to crash. They are UB, so a build may appear to work for months. ASan
inserts *red zones* around allocations and *quarantines* freed memory so that a stray access hits a
poisoned byte and is reported immediately with a stack trace (Lesson 16 explains how). That turns a
silent, occasional, exploitable bug into a loud, deterministic test failure. Build every exercise —
and your object-storage project — with `-fsanitize=address,undefined` in debug/CI.

## Drills
1. Reproduce §1, §2, §3 under ASan and record the exact label for each
   (`stack-use-after-return`, `heap-use-after-free`, `double-free`).
2. Take the §1 `bad()` and fix it two ways (return by value; caller passes a buffer). Confirm the
   `-Wreturn-local-addr` warning on the original.
3. Wrap §2 in a `std::unique_ptr<int[]>` and show the use-after-free is now a compile-time
   impossibility (the pointer is gone after the scope).
4. Explain, in terms of §2.1 §3's single rule, why §1–§4 are "the same bug". Write that sentence in
   your own words.

## My summary
