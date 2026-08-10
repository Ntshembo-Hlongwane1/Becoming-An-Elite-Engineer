# 10 — Operator Overloading, Properly

> You have written `operator=` four times in this series without ever being told what an
> operator overload *is*. This doc covers the whole family: which ones must be members, which
> must be free functions, the canonical form of each, and the conventions that make a type
> behave the way readers expect.
>
> Plus the C++20 feature that replaces six functions with one line — and the missing keyword
> that lets `handle + 1` compile.

---

## 1. Operators are functions with punctuation for names

```cpp
a + b            // is exactly:  operator+(a, b)   or   a.operator+(b)
a = b            // is exactly:  a.operator=(b)
a[i]             // is exactly:  a.operator[](i)
*p               // is exactly:  p.operator*()
```

There is no magic. `operator+` is a function name that happens to contain a symbol, and it obeys
every normal rule: overload resolution, access control, templates, `noexcept`, `constexpr`.

**Why the language allows this at all:** so user-defined types can participate in the same
syntax as built-in ones. `std::string` supports `+`, `std::vector` supports `[]`, iterators
support `*` and `++`. Without it, generic algorithms could not work on both `int*` and
`vector<T>::iterator`.

**And the cost:** an operator carries expectations. `+` should not mutate. `==` should be
symmetric and transitive. `[]` should be cheap. Break those and you have written code that lies
at every call site. Doc §10 is the list.

---

## 2. Member or free function?

Three rules, and the third is the one people get wrong.

### Must be a member

`=` `[]` `()` `->` and conversion operators. The language requires it. This is why `operator=`
is always inside the class.

### Must be free

Any operator where **the left operand is not your type**:

```cpp
std::ostream& operator<<(std::ostream& os, const String& s);   // left operand is a stream
String operator+(int n, const String& s);                       // left operand is an int
```

You cannot add a member to `std::ostream`, so this has no other option.

### Should be free (the one that gets missed)

Binary symmetric operators — `+ - * / == < ` — **should** be free functions, because a member
operator does not allow conversion of the left operand.

```cpp
class String {
    String operator+(const String& rhs) const;    // MEMBER
};

String s = "world";
s + "hello";      // OK:  s.operator+(String("hello"))
"hello" + s;      // ERROR: const char* has no operator+
```

The left operand of a member operator is `*this`, and **no implicit conversion is applied to
it**. As a free function, both sides convert equally:

```cpp
String operator+(const String& lhs, const String& rhs);   // FREE
"hello" + s;      // OK: both operands convert to String
```

**Rule of thumb:** if the operation is symmetric in its operands, make it a free function. If it
modifies the left operand (`+=`, `=`, `++`), make it a member.

---

## 3. The canonical forms

Deviating from these signatures is almost always a bug. Memorise the shapes, not the details.

| Operator | Signature | Returns | Note |
|---|---|---|---|
| `=` | `T& operator=(const T&)` | `*this` | member; doc 04 |
| `=` (move) | `T& operator=(T&&) noexcept` | `*this` | member; doc 05 |
| `+=` | `T& operator+=(const T&)` | `*this` | member; **implement this first** |
| `+` | `T operator+(T lhs, const T& rhs)` | by value | free; built from `+=` |
| `==` | `bool operator==(const T&) const` | `bool` | member is fine in C++20 |
| `<=>` | `auto operator<=>(const T&) const` | ordering | member; §6 |
| `[]` | `T& operator[](size_t)` + const version | reference | member; two overloads |
| `*` | `T& operator*() const` | reference | member |
| `->` | `T* operator->() const` | **pointer** | member; §7 |
| `()` | any | any | member |
| `<<` | `std::ostream& operator<<(std::ostream&, const T&)` | the stream | free |
| `bool` | `explicit operator bool() const` | `bool` | member; **always `explicit`** |
| `++` (pre) | `T& operator++()` | `*this` | member |
| `++` (post) | `T operator++(int)` | **by value** | member; the `int` is a dummy tag |

---

## 4. Build arithmetic from compound assignment

```cpp
class Vec2 {
public:
    Vec2& operator+=(const Vec2& r) {          // 1. the real work, ONCE
        x += r.x; y += r.y;
        return *this;
    }
    friend Vec2 operator+(Vec2 lhs, const Vec2& rhs) {   // 2. built from it
        lhs += rhs;                            // lhs is already a copy -- take by value
        return lhs;
    }
private:
    double x = 0, y = 0;
};
```

Three things worth noticing.

**`operator+` takes `lhs` by value.** You need a copy to return anyway, so let the parameter be
it. That also makes it a sink parameter (doc 05 §5): passing a temporary move-constructs it
instead of copying. Taking `const Vec2&` and making a local copy is strictly worse.

**The logic lives in one place.** `+` cannot drift out of sync with `+=`.

**`friend` inside the class body** defines a free function with access to privates, findable by
ADL — the same technique as `swap` in doc 08 §4.

The `++` pair follows the same shape, and the pre/post asymmetry is a direct consequence of
doc 03 §6:

```cpp
Iter& operator++()    { ++m_Ptr; return *this; }              // pre: returns a reference (lvalue)
Iter  operator++(int) { Iter tmp = *this; ++m_Ptr; return tmp; }  // post: returns a copy (prvalue)
```

Post-increment must return the *old* value, which no longer lives anywhere, so it must copy.
**That is why `++it` is the convention in loops** — for a non-trivial iterator, `it++`
constructs and destroys a temporary every iteration.

---

## 5. Subscript needs two overloads

```cpp
      T& operator[](std::size_t i)       { return m_Data[i]; }
const T& operator[](std::size_t i) const { return m_Data[i]; }
```

Without the `const` version, you cannot index a `const` container at all. Without the non-`const`
version, you cannot assign through it. You need both, and they differ only in constness — which
is the classic case people solve by having the non-const one delegate:

```cpp
T& operator[](std::size_t i) {
    return const_cast<T&>(std::as_const(*this)[i]);     // the Meyers trick
}
```

Legal — the object is genuinely non-const, so removing constness we ourselves added is safe. For
a one-line body it is more obscure than it is worth; for a long one with bounds checks it earns
its keep.

> `operator[]` conventionally does **not** bounds-check (that is `at()`'s job), and it should be
> O(1). Users assume both. A checking, O(n) subscript is a lie in the shape of a `[`.

---

## 6. Comparison in C++20 — one line replaces six

Pre-C++20 you wrote six functions and hoped they stayed consistent. Now:

```cpp
struct V {
    int major, minor, patch;
    auto operator<=>(const V&) const = default;
    bool operator==(const V&) const = default;
};
```

Measured:

```
one defaulted <=> gives all six operators:
  a <  b : 1
  a <= b : 1
  a >  b : 0
  a >= b : 0
  a == b : 0
  a != b : 1
  (a<=>b) < 0 : 1   category: strong_ordering
```

The defaulted `<=>` performs **lexicographic member-wise comparison in declaration order** —
exactly what you want for a version number, and exactly why declaration order matters. The
compiler then synthesises `<`, `<=`, `>`, `>=` from it, and `!=` from `==`.

### Why `==` is declared separately

`<=>` does generate `==`, but comparing for equality via three-way comparison can be slower.
For `std::string`, `==` can first compare lengths and bail; `<=>` must compare content. So the
language keeps them separate and lets you default both.

**Declare both `<=>` and `==` as defaulted** unless you have a reason not to.

### The three ordering categories

| Category | Meaning | Use for |
|---|---|---|
| `strong_ordering` | equivalent values are **indistinguishable** | integers, strings, your `page_id_t` |
| `weak_ordering` | equivalent values may differ observably | case-insensitive strings |
| `partial_ordering` | some pairs are **unordered** | floating point (`NaN`) |

`double` members give you `partial_ordering` automatically, which is correct and occasionally
surprising: `a < b`, `a == b`, and `a > b` can all be false at once.

---

## 7. `operator*` and `operator->` — the smart-pointer shape

Relevant to your `PageGuard`, which currently exposes `Get()`:

```cpp
class PageGuard {
public:
    Page& operator*()  const { return *m_Page; }
    Page* operator->() const { return  m_Page; }    // returns a POINTER, not a reference
};
```

`operator->` is the odd one in the whole language. It returns a pointer, and the compiler then
**applies `->` again to that pointer** — recursively, until it reaches a raw pointer. So
`guard->data` becomes `guard.operator->()->data`. This "drill-down" is what lets proxy and
wrapper types nest transparently.

> Whether `PageGuard` *should* have these is a design question, not a technical one. Doc 11 §3
> argues it should not: a guard owns a pin, and making it look like a pointer to a `Page`
> encourages storing the `Page*` beyond the guard's lifetime — the exact bug the guard exists to
> prevent. `unique_ptr` has them because a smart *pointer* should look like a pointer. A *lock*
> should not.

---

## 8. Conversion operators — always `explicit`

```cpp
struct Handle {
    int fd = -1;
    explicit operator bool() const { return fd >= 0; }
};
```

`explicit` still permits **contextual conversion** — `if (h)`, `while (h)`, `!h`, `h && x` all
work, because those positions request a `bool` unambiguously. What it blocks is everything else.

Without it:

```
NonExplicit silently converts to int: 1  <-- why explicit matters
and allows nonsense:  n + 1 = 2
```

`n + 1` compiled. A file handle was converted to `bool`, promoted to `int`, and added to 1.
Every arithmetic and comparison operator in the language now silently accepts your type. That is
how `if (handle == otherHandle)` ends up comparing two `bool`s and returning true for two
completely different open files.

**Every conversion operator should be `explicit`** unless you have a specific reason. Same
reasoning as `explicit` on single-argument constructors (`storage/03` §9.1): implicit
conversions you did not intend are a defect, not a convenience.

---

## 9. Ref-qualifiers — overloading on the object's value category

You can qualify a member function by whether `*this` is an lvalue or an rvalue:

```cpp
class Buffer {
public:
    const std::vector<char>& data() const&  { return m_Data; }             // lvalue *this
    std::vector<char>        data() &&     { return std::move(m_Data); }   // rvalue *this
};

Buffer b;
auto x = b.data();                 // lvalue -> returns a reference, no copy
auto y = makeBuffer().data();      // rvalue -> MOVES the buffer out of the temporary
```

The second overload matters because the temporary is about to be destroyed, so stealing its
buffer is free. Without the ref-qualified overload you would either copy, or return a reference
into an object that dies at the semicolon (doc 02 §3).

The other use is prohibitive: `T& operator=(const T&) &;` — with the trailing `&` — makes
assignment to a temporary a compile error, so `getWidget() = x;` stops compiling. The built-in
types already behave this way (`5 = x` is an error); ref-qualifying `operator=` makes your type
consistent with them.

Rarely needed. Worth recognising when you meet it in library code.

---

## 10. What not to overload

| Operator | Why not |
|---|---|
| `&&` `\|\|` | Overloads **lose short-circuiting** — both operands always evaluate |
| `,` | Nobody expects it; evaluation order surprises |
| `&` (address-of) | Breaks every generic algorithm that takes an address |
| `->*` `.*` | Almost nobody knows what they do |

And these **cannot** be overloaded at all: `.` `::` `?:` `sizeof` `alignof` `typeid`
`static_cast` and friends.

### The real rule

> **Overload an operator only when the meaning is obvious and matches the built-in intuition.**

`+` on a vector, `==` on a value type, `[]` on a container, `<<` on a stream — all obvious.
`+` meaning "insert into a database", `%` meaning "format a string" — clever, and a permanent
tax on every reader.

The test: *would a competent C++ programmer who has never seen my class guess correctly what
this does?* If not, write a named method. `tree.Insert(k, v)` is better than `tree += {k, v}`,
and it is why your `BPlusTree` has no operators at all — correctly.

---

## Checkpoint

- [ ] Reproduce the `<=>` output; add a `double` member and observe the category change to
      `partial_ordering`
- [ ] Reproduce the non-`explicit` `operator bool` nonsense; try `n * 3`, `n < 2`, `-n`
- [ ] Write `Vec2` with `+=` and a free `+`; verify with `Tracer` that `a + b + c` costs what
      you expect
- [ ] Make `operator+` a member and confirm `"hello" + s` fails; then make it free and confirm
      it compiles
- [ ] Implement both `operator[]` overloads; remove the `const` one and find the first line that
      stops compiling
- [ ] Write the pre/post `++` pair and explain, from doc 03, why they return different categories
- [ ] Answer: *why must `operator->` return a pointer rather than a reference?*
- [ ] Answer: *why should a symmetric binary operator be a free function?*

Next: [11 — Case Studies](11-case-studies.md), where all of this is applied to the code you have
already written.
