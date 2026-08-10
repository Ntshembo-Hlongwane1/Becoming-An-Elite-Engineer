# 08 — Exception Safety and Copy-and-Swap

> Doc 07 said `std::vector` refuses a throwing move because it would break the *strong exception
> guarantee*. This doc names the four guarantees, shows which one your code provides, and then
> presents an idiom that hands you the strong guarantee **and** self-assignment safety **and**
> unified copy/move assignment from a single function.
>
> Measured, one `operator=` handling all three cases correctly:
>
> ```
> copy assignment (lvalue):  COPY ctor -> swap
> move assignment (rvalue):  MOVE ctor -> swap
> self-assignment:           COPY ctor -> swap   (value preserved)
> ```

---

## 1. The four guarantees

Every function you write provides one of these. Knowing which is the difference between code
that degrades and code that corrupts.

| Guarantee | If an exception escapes… | Example |
|---|---|---|
| **No-throw** (`noexcept`) | cannot happen | move ctor, `swap`, destructors |
| **Strong** | the object is *exactly* as it was; the operation was atomic | `vector::push_back` |
| **Basic** | the object is valid and destructible, but its value may have changed | most operations |
| **None** | the object may be corrupt; UB, leaks, dangling | `BrokenString` in doc 01 |

**Basic is the minimum acceptable.** "None" is not a guarantee, it is a bug — it means an
exception leaves your program in a state where continuing is undefined and even destroying the
object is unsafe.

**Strong is what you want at operation boundaries** — a transaction, a `push_back`, an insert
into an index. It gives the caller a meaningful choice: catch, and know nothing happened.

**No-throw is what you want in the primitives everything else is built from** — swap, moves,
destructors — because they are the operations that recovery paths use, and a recovery path that
can itself fail is not a recovery path.

> Notice the layering: **the strong guarantee is usually built out of no-throw pieces.** You do
> the fallible work off to the side, then commit with a no-throw step. That is the entire trick,
> and §3 is one instance of it.

---

## 2. Where your assignment operator sits today

From doc 04 §4:

```cpp
String& operator=(const String& o) {
    if (this == &o) return *this;
    char* fresh = new char[o.m_Size + 1];        // (a) can throw
    std::memcpy(fresh, o.m_Data, o.m_Size + 1);  // (b) cannot
    delete[] m_Data;                             // (c) cannot
    m_Data = fresh;                              // (d) cannot
    m_Size = o.m_Size;
    return *this;
}
```

This is **strong**, and the ordering is why. The only operation that can throw is (a), and at
that point nothing has been modified — `m_Data` still holds the original buffer. If `new`
throws, the object is untouched.

Now the version that is only **basic**:

```cpp
String& operator=(const String& o) {
    delete[] m_Data;                             // destroy first
    m_Data = new char[o.m_Size + 1];             // then acquire -- if THIS throws...
    ...
}
```

If `new` throws here, `m_Data` is a dangling pointer to freed memory. The object is *not* valid
— the destructor will double-free it. That is actually **no guarantee at all**, not even basic.

Assigning `m_Data = nullptr` immediately after the `delete[]` would restore the basic guarantee:
the object becomes empty rather than corrupt. Still worse than the strong version, and no
cheaper.

> **The rule that generalises: do everything that can fail before you modify anything.** Acquire,
> then commit. This is the same discipline as `storage/07` §4's eviction ordering — flush before
> erasing the page-table entry — and `storage/04` §7's header-last write. It is one idea wearing
> three hats.

---

## 3. Copy-and-swap

```cpp
class String {
public:
    friend void swap(String& a, String& b) noexcept {
        std::swap(a.m_Data, b.m_Data);
        std::swap(a.m_Size, b.m_Size);
    }

    // ONE operator, taking its parameter BY VALUE
    String& operator=(String other) noexcept {
        swap(*this, other);
        return *this;
    }
    // ... destructor, copy ctor, move ctor as before ...
};
```

Three lines, and it gives you four properties.

### How it works

The parameter `other` is constructed **from the argument**, using whichever constructor the
argument's value category selects (doc 03 §7). Then we swap our guts with it. Then `other` —
now holding *our old value* — is destroyed at the end of the function, releasing what we used
to own.

Measured:

```
copy assignment (lvalue argument):
  ctor(aaa)  ctor(bbb)
  COPY ctor              <- parameter copy-constructed from the lvalue
  operator=(by value)
  swap
  dtor(aaa)              <- the parameter destroys our OLD value
  a is now: bbb

move assignment (rvalue argument):
  ctor(aaa)  ctor(bbb)
  MOVE ctor              <- parameter MOVE-constructed from the rvalue
  operator=(by value)
  swap
  dtor(aaa)
  a is now: bbb

self-assignment:
  ctor(aaa)
  COPY ctor              <- a copy of ourselves
  swap
  dtor(aaa)
  a is now: aaa          <- correct, no guard needed
```

### What it buys

**1. Strong exception safety, structurally.** The only thing that can throw is constructing the
parameter — which happens *before the function body runs*, and therefore before `*this` has been
touched. If it throws, the assignment never began. You cannot get this wrong by reordering,
because there is no ordering to get wrong.

**2. Self-assignment safety, free.** `a = a` copy-constructs the parameter from `a` (a real,
separate copy), then swaps. No aliasing, no guard, no branch. Compare doc 04 §5, where the naive
version returned garbage.

**3. One function instead of two.** The by-value parameter is copy-constructed from lvalues and
move-constructed from rvalues, so a single `operator=` provides both copy assignment *and* move
assignment. Five special members become four.

**4. No code duplication.** All the resource logic lives in the copy constructor and destructor.
The assignment operator contains no `new`, no `delete`, and no knowledge of what the class owns —
so adding a member means updating one place, not three.

### The cost — and it is real

**It always allocates.** Consider assigning a 10-byte string into a `String` that already has a
1 KB buffer:

- **Copy-and-swap:** allocate 11 bytes, copy, swap, free 1 KB. One allocation, one deallocation.
- **A hand-written `operator=`:** notice the existing buffer is big enough, `memcpy` into it,
  update the size. **Zero allocations.**

This is exactly why `std::string` and `std::vector` do **not** use copy-and-swap for their
assignment operators. Buffer reuse in assignment is a major optimisation for them, and it is
worth the extra code and the self-assignment guard.

**A second, subtler cost:** with a by-value `operator=`, move assignment costs a move-construct
plus a swap, rather than the direct steal a hand-written `operator=(T&&)` would do. Usually
negligible; occasionally not.

### When to use it

| Use copy-and-swap | Write them separately |
|---|---|
| Correctness matters more than the last few percent | The type is used in a hot path |
| The class is not assigned in a tight loop | Buffer reuse is a real win |
| You want the strong guarantee without thinking | You have measured, and it matters |
| Default choice | Optimisation, justified by a number |

**Start with copy-and-swap.** It is much harder to get wrong. Replace it only when a profiler
tells you to.

---

## 4. Why `swap` is a `friend` free function

```cpp
friend void swap(String& a, String& b) noexcept;
```

Two reasons, and the second is the one people miss.

**It needs private access** — hence `friend`. Defining it inside the class body as a `friend`
gives it access without a separate declaration.

**Generic code finds it by ADL.** The standard idiom in templates is:

```cpp
using std::swap;        // fallback
swap(a, b);             // unqualified -- ADL finds YOUR swap if it exists
```

Argument-dependent lookup searches the namespace of the argument's type. A `friend` function
defined inside the class is found by ADL and by nothing else — which is exactly right, since it
should only ever be called on this type. Write `std::swap(a, b)` explicitly instead and you get
the generic three-move version, missing your specialisation entirely.

**`swap` must be `noexcept`.** The whole edifice rests on it: it is the commit step of the
strong guarantee. A `swap` that can throw provides nothing. For a resource-owning class this is
easy to honour — swapping pointers cannot fail.

---

## 5. Making the guarantee explicit in your own code

The four guarantees are worth **writing down** in a comment on any non-trivial operation, because
callers cannot infer them and the compiler cannot check them.

```cpp
// Strong: if this throws, the tree is unmodified.
void DiskBPlusTree::Insert(disk_key_t key, const PostingRef& value);

// Basic: on failure the pool is valid but the page may or may not be resident.
Page* BufferPool::FetchPage(page_id_t pageId);

// No-throw.
void BufferPool::UnpinPage(page_id_t pageId, bool isDirty) noexcept;
```

Being honest here is more useful than being ambitious. `Insert` in your on-disk tree is
realistically **basic**, not strong — a split that fails partway through leaves allocated pages
that are not reachable. Saying so tells the caller their recovery option is "reopen the file,"
not "retry the insert."

---

## Checkpoint

- [ ] Implement copy-and-swap on your `String` and reproduce all three traces
- [ ] Confirm that the **five** special members become **four** (destructor, copy ctor, move
      ctor, one `operator=`)
- [ ] Write the delete-then-allocate `operator=` and reason about what state the object is in if
      `new` throws. Then explain why that is worse than "basic"
- [ ] Make `swap` a member instead of a `friend` free function, and write a template that calls
      unqualified `swap(a, b)`. Observe which one is selected
- [ ] Benchmark copy-and-swap against a hand-written `operator=` for repeated assignment of a
      short string into a long one. Confirm the allocation cost is real
- [ ] Annotate three functions in `internal/kernal/` with their actual guarantee
- [ ] Answer: *why does copy-and-swap handle self-assignment without a guard?*
- [ ] Answer: *why don't `std::string` and `std::vector` use it?*

Next: [09 — Copy Elision, RVO, NRVO](09-copy-elision.md), where the compiler removes the copies
and moves you have been carefully writing — and where `return std::move(x)` turns out to be
slower than `return x`.
