# 07 — `noexcept` and Why Containers Care

> **The measurement this doc is built on:** the same class, pushed into the same `std::vector`,
> differing only in whether its move constructor is marked `noexcept`.
>
> ```
> move ctor IS noexcept        copies=0    moves=25
> move ctor is NOT noexcept    copies=15   moves=10
> ```
>
> One keyword. Fifteen deep copies. And nothing warns.

---

## 1. What `noexcept` actually says

```cpp
String(String&& o) noexcept;
```

It is a **promise to the compiler and to the standard library**: *this function will never let an
exception escape.*

It is not a request, a hint, or a wish. It is checked at runtime, and the penalty for breaking
it is severe:

```cpp
void liar() noexcept { throw std::runtime_error("I promised not to"); }

try { liar(); } catch (...) { printf("caught\n"); }     // does NOT catch
```

```
calling a noexcept function that throws ->
terminate called after throwing an instance of 'std::runtime_error'
  what():  I promised not to
---- exit: 3 ----
```

**`std::terminate` is called immediately.** The `catch (...)` never runs. There is no stack
unwinding, no destructors, no cleanup — the process dies where it stands.

GCC warns when it can see the violation statically:

```
warning: 'throw' will always call 'terminate' [-Wterminate]
```

But it only sees the obvious cases. A `noexcept` function that calls something that throws three
layers down gets no warning at all.

> `noexcept` is not "this function has no `throw` statement." It is "**nothing this function
> calls will ever throw**" — including `new`, which throws `std::bad_alloc`, and every standard
> container operation that allocates.

---

## 2. Why `std::vector` cares — the strong exception guarantee

`std::vector` must keep a promise of its own: **if `push_back` throws, the vector is unchanged.**
That is the *strong exception guarantee* (doc 08 §2), and it is what makes vector usable in
code that must not corrupt state on failure.

Now watch the problem. `push_back` needs to grow:

1. Allocate a new, larger block.
2. Transfer all existing elements into it.
3. Free the old block.

If step 2 **moves** the elements and throws halfway through, the situation is unrecoverable.
Half the elements now live in the new block; the other half are in the old block; and the ones
already moved have been **gutted** — their contents are in the new block. There is no way back:
moving them back could throw again, and the originals no longer hold their values.

If step 2 **copies** and throws, recovery is trivial: destroy whatever was copied into the new
block, free it, and the original vector is untouched. The originals were never modified.

So vector's rule is:

> **Use the move constructor only if it is `noexcept`. Otherwise copy.**

Not because copying is better — because a throwing move makes the strong guarantee
*impossible to provide*, and vector will not break its contract to make your code faster.

### The mechanism: `std::move_if_noexcept`

```cpp
template <typename T>
constexpr std::conditional_t<
    !std::is_nothrow_move_constructible_v<T> && std::is_copy_constructible_v<T>,
    const T&,          // -> selects the COPY constructor
    T&&                // -> selects the MOVE constructor
> move_if_noexcept(T& x) noexcept;
```

Read the condition: *"if the move can throw **and** a copy is available, hand back a `const T&`
so the copy constructor is chosen."*

Note the second clause. **If the type is move-only, vector uses the throwing move anyway** —
because the alternative is not compiling at all. In that case vector silently downgrades to the
*basic* guarantee. Your `PageGuard` is in exactly this category, which is a reason to be certain
its move really cannot throw.

---

## 3. The measurement

```cpp
template <bool NX>
struct T {
    int* p;
    T(): p(new int(1)) {}
    ~T(){ delete p; }
    T(const T& o): p(new int(*o.p)) { ++g_copies; }
    T(T&& o) noexcept(NX) : p(o.p) { o.p = nullptr; ++g_moves; }
};

std::vector<T<NX>> v;
for (int i = 0; i < 10; ++i) v.push_back(T<NX>{});
```

```
move ctor IS noexcept        nothrow_move=yes  copies=0    moves=25   final capacity=16
move ctor is NOT noexcept    nothrow_move=NO   copies=15   moves=10   final capacity=16
```

Reading it: 10 of the moves in both rows are the `push_back` temporaries being moved into place —
those happen either way, because the argument is an rvalue and vector uses it directly. The
difference is the **15 reallocation transfers**, as the vector grows 1 → 2 → 4 → 8 → 16. With
`noexcept` those are moves; without, they are deep copies that allocate.

Scale that up. A `vector<std::string>` holding 10,000 strings of 1 KB, growing once: with
`noexcept`, 10,000 pointer swaps. Without, **10 MB of allocation and copying**, silently, on a
line that says `push_back`.

`noexcept(NX)` in that code is the **conditional** form — `noexcept(true)` or
`noexcept(false)` depending on a compile-time boolean. §6 shows the useful version of it.

---

## 4. The connection back to doc 06

This is where the two rules compound, and it is the most important paragraph in the doc.

```
Q (has ~Q):    nothrow_move_constructible = NO
R (rule of 0): nothrow_move_constructible = yes
std::string:   nothrow_move_constructible = yes
```

```cpp
struct Q { std::string s; ~Q(){} };      // an empty destructor
struct R { std::string s; };             // nothing
```

Follow the chain:

1. `~Q(){}` is user-declared → **doc 06 Rule S**: the implicit move constructor is not generated.
2. Moving a `Q` therefore selects the **copy** constructor.
3. The copy constructor copies a `std::string` → it allocates → **it can throw `bad_alloc`**.
4. So `is_nothrow_move_constructible_v<Q>` is **false**.
5. So `std::vector<Q>` **deep-copies every element on every reallocation.**

**One empty destructor, and every `vector<Q>` operation silently allocates.** Neither step
produces a diagnostic. This is doc 06's trap and doc 07's trap meeting, and it is the single
most valuable thing to take from this series.

---

## 5. When to mark `noexcept`

**Always, on these — they are part of the type's contract:**

- **Move constructor and move assignment.** A correct move only transfers pointers and nulls the
  source; nothing in it can throw. If yours *can* throw, that is a design problem — it means the
  move is allocating, which defeats its purpose.
- **`swap`.** The standard library and copy-and-swap (doc 08) both depend on it.
- **Destructors.** Already implicitly `noexcept`; do not un-mark them.
- **Simple observers** — `size()`, `empty()`, trivial getters.

**Never, on these:**

- Anything that allocates (`new`, container insertion, `std::string` construction).
- Anything that may throw a domain error.
- **Anything you are not certain about.** `noexcept` is a promise enforced by `std::terminate`;
  an incorrect one converts a recoverable error into a process kill.

> The asymmetry favours caution in one direction only: a missing `noexcept` costs performance
> (measurably, as above). An *incorrect* `noexcept` costs you the process. Be generous where you
> are certain and silent where you are not.

---

## 6. `noexcept` as an operator, and the conditional form

`noexcept` has a second, unrelated meaning: an operator that asks, at compile time, whether an
expression can throw.

```cpp
static_assert(noexcept(a.swap(b)));       // compile-time query, no code generated
```

Combined with the specifier, this gives you the correct way to propagate the property through a
generic type:

```cpp
template <typename T>
class Wrapper {
    T m_Value;
public:
    // "I am nothrow-movable exactly when T is."
    Wrapper(Wrapper&& o) noexcept(std::is_nothrow_move_constructible_v<T>)
        : m_Value(std::move(o.m_Value)) {}
};
```

Hard-coding `noexcept` here would be a lie for a `T` whose move can throw, and omitting it would
be a pessimisation for the far more common `T` whose move cannot. The conditional form is the
only correct answer for a template.

This is exactly what the standard library does throughout, and it is why
`is_nothrow_move_constructible_v<std::string>` came back `yes` above — `std::string`'s move is
unconditionally `noexcept`, and every container built on it inherits the property.

---

## 7. Where else `noexcept` changes behaviour

Beyond vector reallocation:

- **`std::vector::resize`, `reserve`, `insert`, `emplace`** — same `move_if_noexcept` logic.
- **`std::swap`** — the generic version is `noexcept` if the moves are, which propagates to every
  algorithm using it.
- **Code generation.** A `noexcept` function needs no unwinding tables for its own frame, so the
  compiler can omit landing pads and sometimes inline more aggressively. Small, real, and a
  distant second to the container effect.
- **`std::terminate` handlers** and contract-checking tools read it.

---

## Checkpoint

- [ ] Reproduce the copies/moves measurement. Vary the element count and watch the copy count
      track the reallocation points (1, 2, 4, 8…)
- [ ] Reproduce the `Q` vs `R` result — the doc 06 → doc 07 chain is the payoff of both docs
- [ ] Write a `noexcept` function that throws and confirm `catch (...)` does not catch it
- [ ] Take your `PageGuard`, remove `noexcept` from its move constructor, and try to
      `std::vector<PageGuard> path;` with `push_back`. Read the error and explain it in terms of
      `move_if_noexcept`
- [ ] Add the conditional `noexcept(...)` form to a template of your own
- [ ] Answer: *why can't `std::vector` provide the strong guarantee with a throwing move?*
- [ ] Answer: *why does vector use a throwing move anyway for a move-only type?*

Next: [08 — Exception Safety & Copy-and-Swap](08-exception-safety.md), which names the guarantee
vector is protecting and shows the idiom that gives it to you for free.
