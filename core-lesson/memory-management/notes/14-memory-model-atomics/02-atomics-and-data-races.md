# 14.2 — Atomics: fixing the race

## 1. What an atomic operation is

`[MEM §15.2]`: "Atomic operations are indivisible and ensure that no other thread can observe a
partially completed operation." That's the first guarantee: an atomic load/store/read-modify-write
happens all-at-once — no other thread sees it half-done (no **torn** read/write, §14.5). The second,
bigger guarantee is that atomics carry a **memory ordering** that lets you establish happens-before
across threads (§14.3). Together they are path (b) from §14.1 §2: "you can also avoid the undefined
behavior by using atomic operations to access the memory location involved in the race" `[CIA §5.1.2]`.

`std::atomic<T>` (from `<atomic>`) wraps a `T` so that `.load()`, `.store()`, `.exchange()`,
`.fetch_add()`, `.compare_exchange_*()` are atomic. It works for integers, pointers, `bool`, and any
**trivially copyable** `T` (though only small ones are *lock-free* in hardware; larger `atomic<T>` may
use an internal lock — check `is_lock_free()`).

## 2. Fixing the §14.1 race

The plain-`int` handshake was UB. Making the flag atomic turns the race into a defined, ordered
handoff. `[MCCP §5.1.2]`:

```cpp
std::atomic<int> flag{0};
int data = 0;                         // still a plain int — and that's fine now

void writer() { data = 42; flag.store(1, std::memory_order_release); }   // publish
void reader() { while (!flag.load(std::memory_order_acquire)) {}          // observe
                int x = data; }                                           // sees 42, guaranteed
```

`data` stays a plain `int`, yet reading it is now **race-free**. Why? The release store on `flag`
**synchronizes-with** the acquire load that reads it (§14.3), which makes the writer's `data = 42`
(sequenced before the release) **happen-before** the reader's `int x = data` (sequenced after the
acquire). The ordering the §14.1 race lacked now exists. `[MCCP §5.1.2]`: "The release-acquire pair
establishes a happens-before relationship, making the program well-defined."

### (measured) the atomic version is TSan-clean
Building the corrected version with ThreadSanitizer on your VM:
```
=== atomic (release/acquire) version under TSan ===
exit=0  (no output above = TSan clean)
```
`(measured; norace.cpp)` No data-race warning — the atomic established the ordering TSan was looking
for. The *only* change from the racy version was making `flag` atomic with release/acquire.

## 3. Atomic ≠ mutex, and atomic ≠ lock-free (yet)

Three distinct things people conflate:
- **Atomic vs mutex.** Both remove the data race (§14.1's two paths). A mutex makes a whole *critical
  section* mutually exclusive (coarse, can block/deadlock); an atomic makes a *single operation*
  indivisible (fine, never blocks). You use atomics to build lock-free structures (no mutex at all).
- **Atomic vs lock-free.** A single atomic op is always "lock-free" in the trivial sense, but a
  *lock-free algorithm* is a bigger claim: a data structure where threads coordinate using atomics and
  **retries** such that the system always makes progress (§14.4, `[MCCP §11.1.1]`). You can misuse
  atomics and still be blocking or broken.
- **Atomic ≠ thread-safe object.** `std::atomic<int> counter` is safe to increment concurrently;
  wrapping a `std::vector` in `std::atomic` is not possible (not trivially copyable) and wouldn't make
  the vector's operations safe anyway. Atomicity is per-operation on the atomic variable.

## 4. The counter example (and why `relaxed` is enough there)

`[MCCP]`'s lock-free counter:
```cpp
std::atomic<int> counter{0};
counter.fetch_add(1, std::memory_order_relaxed);   // atomic increment, no ordering needed
```
`fetch_add` is a single atomic read-modify-write, so concurrent increments don't lose updates (no
race). Here `memory_order_relaxed` is sufficient `[MCCP]`: "The atomic operation eliminates the data
race. `memory_order_relaxed` is sufficient because" the counter's value doesn't *order* anything else —
no other memory is being published through it. That's the key intuition for §14.3: **use the weakest
ordering that still establishes the happens-before you actually need** — and a bare counter needs
none.

## Drills
1. Take `norace.cpp`; change the release/acquire to `memory_order_relaxed` on both. Does TSan complain
   again? (On x86 it may *run* fine but TSan should still flag the missing synchronization — relaxed
   doesn't publish `data`.) Explain via §14.3.
2. Write a shared counter incremented by 8 threads 100k times each, once with `fetch_add(relaxed)` and
   once with `counter++` on a plain `int`. Check the final value and run both under TSan. Which is
   correct, which races, and why is relaxed enough for the atomic one (§4)?
3. Is `std::atomic<double>` lock-free on your machine? Print `std::atomic<double>{}.is_lock_free()` and
   `std::atomic<SomeBigStruct>{}.is_lock_free()`. What does a *false* result imply the library is doing?
4. Explain why making `flag` atomic lets `data` stay a plain `int` and still be race-free (§2) — which
   relationship does the release/acquire create, and over which memory?

## My summary
