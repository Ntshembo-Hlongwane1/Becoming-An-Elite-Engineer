# Startup & Boot Architecture — Part 2: Persistence & the Warm Start

> **Answers your Question 1:** *"When the program starts, the Store should tell the Kernal
> whether a saved search index exists — and if so, the Engine starts immediately."*
>
> This part teaches the **C++ practice** for: detecting a saved index on disk, loading it,
> and — crucially — **how the Store reports the answer back to the Kernal**. You will not
> get finished code; you will get the mental models, the idioms, and skeletons with the
> load-bearing logic left for you.

---

## 1. First, what *is* "the search index"? (Get concrete before touching disk)

Look at your `Store`:

```cpp
std::unordered_map<std::string, std::vector<std::string>> searchIndex_;
//                  ^key: token          ^value: list of documents containing it
```

That is an **inverted index** (word → documents) — exactly what `overview.md` Version 4
describes. "Saving the index" means: take this in-memory hash map and write it to disk in
a form you can read back later to reconstruct the *same* map. "Does a saved index exist?"
means: is there a file on disk that holds a previously-serialized version of this map?

> **Systems framing:** an in-memory data structure and its on-disk representation are two
> different things. Converting memory → bytes-on-disk is **serialization**; bytes-on-disk
> → memory is **deserialization**. Every database, cache, and save-file does this. The map
> lives in RAM (fast, volatile, lost on exit); the file lives on disk (slower, durable,
> survives restarts). The whole point of persistence is bridging that gap.

---

## 2. The three questions persistence forces you to answer

Before any C++, decide these (they're design decisions, not lookups):

1. **Where does the file live?** A fixed path like `index.dat`? Next to `data/`? This is a
   policy choice. Pick something and be consistent.
2. **What format?** Text (human-readable, easy to debug, larger, slower) or binary
   (compact, fast, opaque)? For a *learning* project, start with **text** — you can open it
   in an editor and see whether your serialization is correct. Optimize to binary later
   (there's a whole `performance.md` for when that day comes).
3. **How do you know the file is valid, not just present?** A file can exist but be
   empty, truncated (crash mid-write), or from an older format. "Exists" and "usable" are
   different claims. More on this in §6.

> **Your turn:** write down your answers to these three before continuing. The rest of the
> doc assumes you've chosen a path and text format, but the *techniques* are format-agnostic.

---

## 3. Detecting existence — the C++ practice (`std::filesystem`)

You already use `<filesystem>` in `DirectoryReader::OnInit()`:

```cpp
if (!std::filesystem::exists("data") || !std::filesystem::is_directory("data")) { ... }
```

Same toolkit applies. The relevant functions (all in `namespace std::filesystem`, header
`<filesystem>`):

| Function | Answers | Go analogue |
|---|---|---|
| `exists(p)` | Is there anything at path `p`? | `os.Stat` + `os.IsNotExist` |
| `is_regular_file(p)` | Is it an ordinary file (not a dir/symlink)? | `info.Mode().IsRegular()` |
| `file_size(p)` | How many bytes? (throws if missing) | `info.Size()` |

### The trap: the throwing vs non-throwing overloads

This is a genuine C++ gotcha worth internalizing. Most `std::filesystem` functions have
**two overloads**:

```cpp
bool exists(const path& p);                              // (A) throws on error
bool exists(const path& p, std::error_code& ec) noexcept;// (B) reports via ec, never throws
```

- Overload **(A)** throws `std::filesystem::filesystem_error` if it can't even *check*
  (e.g., permission denied on a parent directory). For a simple "does it exist" you often
  don't want an exception to escape your startup path.
- Overload **(B)** takes an `std::error_code&` out-parameter and sets it instead of
  throwing. `ec` is falsy when the call succeeded.

For startup code that must be robust, prefer the `error_code` overload — you're checking
existence precisely *because* you're unsure, so "the check itself failed" is a case you
want to handle, not crash on.

```cpp
std::error_code ec;
bool present = std::filesystem::exists(indexPath, ec);
if (ec) {
    // The check itself failed (permissions, bad path). Decide: treat as "no index"?
    //   Log it and fall through to cold start is usually the safe choice.
}
```

> **Mental model:** "the file isn't there" and "I couldn't tell whether the file is there"
> are different outcomes. A beginner conflates them; a systems engineer handles both.

---

## 4. Loading the index — RAII file reading

If the file exists and looks valid, you load it. This is `<fstream>` territory and it's
covered in depth in `cpp-file-io.md` — read that. The one principle to carry here:

**RAII means the file closes itself.** You open an `std::ifstream`, read from it, and when
it goes out of scope the destructor closes the handle — even if an exception is thrown
mid-read. You never call `close()` and you never leak a descriptor.

```cpp
{
    std::ifstream in(indexPath);        // opens here
    if (!in.is_open()) { /* handle */ } // opening can still fail even if exists() was true
    // ... read lines, rebuild the map ...
}                                        // closes here, automatically
```

Note the belt-and-suspenders: `exists()` was true a moment ago, but between the check and
the open the world can change (a "time-of-check to time-of-use" race), and opening can
fail for reasons existence doesn't cover. **Always check `is_open()` after opening**, not
just `exists()` before.

### The shape of loading (skeleton — you fill the parsing)

```cpp
// Store method, conceptually. Return type discussed in §7 — don't fixate on it yet.
??? Store::LoadIndex(const std::filesystem::path& p) {
    std::ifstream in(p);
    if (!in.is_open()) return /* "couldn't open" */;

    std::string line;
    while (std::getline(in, line)) {
        // YOUR JOB: parse one serialized entry back into searchIndex_.
        //   - Split the line into (token, [docs...]) using whatever format you chose.
        //   - searchIndex_[token].push_back(doc) for each doc.
        // Think: what delimiter did you write? How do you handle a doc name with that
        //   delimiter inside it? (This is why formats are a design decision.)
    }

    if (in.bad()) return /* "read error mid-file" */;   // hardware/stream failure
    return /* "loaded N entries successfully" */;
}
```

> **Your turn:** design the exact line format you'll write in §5 and parse here. The
> reader and writer must agree perfectly — they are two halves of one contract. A great
> exercise: write the format down as a mini-grammar (you have `ebnf-notation.md` for
> inspiration).

---

## 5. Saving the index — the other half of the contract

You can't *load* an index until something *saved* one. On a cold start, after the Parser
finishes building the index (Part 4), someone persists it so the *next* run is warm.

```cpp
{
    std::ofstream out(indexPath);       // truncates/creates
    if (!out.is_open()) return /* error */;
    for (const auto& [token, docs] : store.GetSearchIndex()) {
        // YOUR JOB: write one line per entry in the format LoadIndex() expects.
    }
}   // flushes + closes on scope exit
```

Two durability subtleties worth knowing (systems depth, optional for v1):

- **Flushing.** Data you `<<` into a stream may sit in a buffer until it's flushed. The
  destructor flushes on close, but if the process is killed mid-write you can get a
  truncated file — which is exactly why §6 matters.
- **Atomic replace.** Production systems write to a temp file (`index.dat.tmp`) and then
  `std::filesystem::rename()` it over the real name. `rename` is atomic on most systems,
  so a reader never sees a half-written index — it sees either the old one or the new one.
  You don't need this for v1, but know it exists; it's how databases avoid corrupt saves.

---

## 6. "Exists" ≠ "valid": defensive loading

A file at `indexPath` could be:

- **Present and good** → warm start.
- **Present but empty** (0 bytes — e.g., a crash right after create) → treat as no index.
- **Present but truncated/corrupt** (crash mid-write) → don't trust it; cold start.
- **Present but wrong format** (from an older version of your code) → cold start or migrate.

The robust posture: **if anything about loading looks wrong, fall back to a cold start.**
A cold start is slow but *always correct* — it rebuilds from the source documents. A
corrupt warm start gives *wrong search results silently*, which is far worse.

```
present? ──no──► cold start
   │yes
   ▼
size > 0? ──no──► cold start
   │yes
   ▼
parses cleanly? ──no──► cold start (and maybe delete/rename the bad file)
   │yes
   ▼
warm start
```

> **Systems insight:** this is the "fail safe, not just fail fast" principle. Real
> indexers store a small header — a magic number and a version — at the top of the file so
> they can reject foreign or outdated files instantly. A single integer at the top that you
> check on load is a cheap, powerful guard. Consider adding one.

---

## 7. The heart of Question 1: **how does the Store *report* the answer?**

This is the real C++ design lesson of Part 2. The Store must communicate one of a few
outcomes to the Kernal. Look at what your subsystems return today:

```cpp
Error Store::OnInit();   // returns only an Error — "" means success
```

An `Error` can say "something went wrong," but it *cannot* cleanly express the tri-state
you need: **loaded a warm index / no index found / found but corrupt**. Trying to encode
that in a string message would be a hack (the Kernal would have to *parse the message* to
decide the boot path — brittle and ugly).

So you need a richer return type. Here are the idiomatic C++ options, worst to best for
this job:

### Option A — magic return values / out-params (avoid)
Returning `int` codes (0/1/2) or an `Error` whose message you string-match. Works, but the
meaning isn't in the type — future-you has to remember what `2` means. Skip it.

### Option B — `enum class` (clean and explicit)
Model the outcomes as a named set:

```cpp
enum class IndexStatus {
    Loaded,      // warm start: index is now in searchIndex_
    NotFound,    // cold start: nothing on disk
    Corrupt      // cold start: file existed but was unusable
};
```

`enum class` (scoped enum) is the modern C++ choice: the values are namespaced
(`IndexStatus::Loaded`, not a bare `Loaded`), they don't implicitly convert to `int`, and a
`switch` over them lets the compiler warn you if you forget a case. This maps *perfectly*
to a `switch` in the Kernal that picks the boot path (Part 3).

### Option C — `std::optional<T>` (when "the thing" is the payload)
`std::optional<T>` (header `<optional>`) means "maybe a T, maybe nothing" — like Go's
`(T, ok bool)` collapsed into one value. If your Store method's job were "give me the
loaded index or nothing," `std::optional` fits:

```cpp
std::optional<Index> tryLoad = store.LoadIndex(path);
if (tryLoad) { /* warm: *tryLoad is the index */ }
else         { /* cold */ }
```

But `optional` only distinguishes *present* vs *absent* — it can't separate "not found"
from "corrupt." If you care about that distinction (§6 says you should), `enum class`
(or `std::expected` in C++23, or your own small struct) is better.

### Option D — a small result struct / `Result` type
Your repo already has a `result.cpp` (paired with `Error`). A result type that carries
*both* a status *and* details ("loaded 8,412 entries", or "corrupt at line 30") is the most
expressive. Look at what `result.cpp` provides and consider whether it already gives you
this shape.

> **Recommendation for you:** start with **Option B, `enum class IndexStatus`**. It's the
> smallest thing that expresses exactly the branch the Kernal needs, it's self-documenting,
> and it forces the Kernal to handle every case. You can always enrich it later.

> **Your turn (design decision, then implement):**
> 1. Add a method to `Store` — something like `IndexStatus LoadIndexIfPresent(path)` — that
>    checks existence, attempts the load, and returns the status. Note: this is *not*
>    `OnInit()`. `OnInit` should stay a pure pre-flight check that returns `Error`. Loading
>    is a distinct concern; give it its own method. (Ask yourself why mixing them would
>    violate the single-responsibility idea from Part 1 §3.)
> 2. Decide who *calls* it. The Store is passive (no threads). Something in the control
>    plane must invoke it during boot. That's the Kernal — which is exactly Part 3.

---

## 8. Const-correctness and return semantics (a C++ depth point)

Look at how the Store already exposes the index:

```cpp
const std::unordered_map<std::string, std::vector<std::string>>& GetSearchIndex() const;
//                                                              ^returns a reference
//     ^const map                                                          ^const method
```

Two deliberate C++ choices here, both worth understanding because Question 1 touches this
same surface:

- **Returns a `const&`, not a copy.** The index can be huge. Returning by value would copy
  the entire map on every call. Returning `const&` hands out a read-only *view* — no copy,
  and the caller can't mutate your internal state. (The `Engine` relies on this in
  `Search()`.) When *loading*, you're going the other direction: filling the map, so you'll
  write to `searchIndex_` directly from inside a Store method.
- **The method is `const`.** The trailing `const` promises "calling this doesn't modify the
  Store." Your load method will *not* be const (it mutates `searchIndex_`), and that's
  correct — the type system documents intent. If you accidentally mark the load method
  `const`, the compiler will stop you from writing to the map. That's the type system
  working *for* you.

> **Go contrast:** Go has no `const`. In C++, `const` is a compile-time contract about
> mutation. Getters are `const` and return `const&`; mutators are non-const. Following this
> convention makes "can this call change my state?" answerable *by reading the signature*.

---

## 9. Putting Question 1 together (the shape, not the solution)

The warm-start half of your desired flow, conceptually:

```
Kernal boot:
   InitAll()                          // pre-flight everyone (cheap, may fail fast)
   status = Store.LoadIndexIfPresent(indexPath)
   switch (status):
     Loaded   → start ONLY the Engine        // ← warm start, this doc's payoff
     NotFound → start the pipeline (Part 3)   // ← cold start
     Corrupt  → (log) start the pipeline      // ← cold start, safe fallback
```

Notice: **the Store produced a fact (`status`); the Kernal made the decision (the
`switch`).** That's the control/data-plane split from Part 1 §3, now concrete.

The `switch` itself — "start only the Engine" vs "start the pipeline" — is the subject of
**Part 3**. You now have the piece it depends on: a Store that can answer, in a type the
Kernal can branch on, whether a usable index already exists.

---

## 10. Before you move on

Check yourself:

1. What's the difference between a file *existing* and a file being *usable*, and why does
   your loader fall back to a cold start when in doubt?
2. Why is the `error_code` overload of `std::filesystem::exists` often the right choice in
   startup code?
3. Why can't `OnInit()`'s `Error` return type express the answer to "does an index
   exist?", and what type *can*?
4. Why does `GetSearchIndex()` return `const&` instead of by value, and why is it `const`?
5. Why should the "load the index" method be *separate* from `OnInit()`?

Next: **Part 3 — Conditional Orchestration**, where the Kernal takes the `IndexStatus` and
chooses a boot path, and where you'll learn how to start a *subset* of subsystems instead
of `StartAll()`.
