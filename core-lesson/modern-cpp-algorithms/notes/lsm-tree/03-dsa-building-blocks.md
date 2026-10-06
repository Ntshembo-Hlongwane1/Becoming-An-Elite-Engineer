# Part 3 — The DSA Building Blocks (Systems View)

> An LSM-tree is not one data structure. It is six classic ones glued together with I/O:
> a **sorted run**, a **comparator**, an in-memory **ordered map** (skip list), an **external
> index** (B-tree-like), a **k-way merge**, and a **Bloom filter**, all protected by **checksums**.
> Here we study each one the way your algorithms book intends — "what does it cost on a real
> machine?" — and we contrast the LSM with the B-tree of `[ALGO ch.11]`.

Contents

1. The master invariant: sorted runs + binary search
2. Comparators: total order over *bytes*
3. Ordered maps in memory: `std::map` (red-black tree) vs. skip list
4. Skip lists in depth (Pugh 1990 + LevelDB)
5. B-trees on disk — and why in-place updates hurt (with a critique of `[ALGO §11]`)
6. Merging: two-way merge, k-way merge, "newest wins"
7. Bloom filters — theory, an experiment exposing a bug in `[ALGO §20.3.2]`, and a correct design
8. Checksums (CRC32C) and the "masked CRC"
9. Summary + drills

---

## 1. Sorted runs + binary search

A **sorted run** is a sequence of `(key, value)` pairs in key order, with each key at most once.
Every on-disk component of an LSM-tree is a sorted run: "The sequence of key/value pairs in the
file are stored in sorted order" `[LDB-TABLE]`; in RocksDB "Each level (except level 0) is one data
sorted run" `[ROCKS-LEVELED]`.

Why sorted? Because sorted data gives you three things for free **(derived)**:

1. **Lookup by binary search** — O(log n) comparisons (`std::lower_bound`).
2. **Range scans** — "all keys between a and b" is one contiguous region → sequential I/O.
3. **Cheap merging** — two sorted runs merge in one linear pass (§6). That linear pass is what
   makes compaction affordable.

RocksDB describes lookup in a level exactly as a two-stage binary search: "we first binary search
the start/end key of all files to identify which file possibly contains the key, and then binary
search inside the file to locate the exact position" `[ROCKS-LEVELED]`.

**Systems view of binary search (derived):** on an array in RAM, each probe is a likely cache
miss for large n (probes jump far apart). On disk, each probe could be a page read. That's why
storage structures *don't* binary-search raw data on disk; they binary-search a **small index
held in memory**, then read **one block**. Bigtable: "A lookup can be performed with a single disk
seek: we first find the appropriate block by performing a binary search in the in-memory index,
and then reading the appropriate block from disk." `[BIGTABLE06 §4]`

## 2. Comparators: a total order over bytes

Everything sorted needs a **comparator**: `cmp(a,b) < 0` if a sorts before b, `0` if equal,
`> 0` after. RocksDB: "The keys are ordered within the key value store according to a
user-specified comparator function" `[ROCKS-BASIC]`. The default is **bytewise**: compare as
unsigned bytes, like `memcmp`, shorter-prefix first.

Requirements for any comparator (classical; a sort is only correct if these hold) **(derived)**:
- **Irreflexive / consistent:** `cmp(a,a) == 0`.
- **Antisymmetric:** sign of `cmp(a,b)` is the opposite of `cmp(b,a)`.
- **Transitive:** a<b and b<c ⇒ a<c.
- **Stable forever.** This one is storage-specific: the order is *baked into files on disk*. If you
  change the comparator, old SSTables are now "unsorted" and binary search silently returns wrong
  answers. (LevelDB records the comparator's *name* in the MANIFEST for this reason —
  `kComparator` is one of the record tags in `[LDB-SRC:db/version_edit.cc]`.)

You saw in Part 2 §4 that numeric order ≠ byte order unless integers are encoded big-endian.

## 3. Ordered maps in memory

The memtable must support: insert, point lookup, and **in-order iteration** (to flush it as a
sorted run). Any ordered map works. O'Neil: "the C0 tree is not expected to have a B-tree-like
structure … the nodes could be any size … Thus a (2-3) tree or AVL-tree … are possible
alternative structures for a C0 tree." `[ONEIL96 §2.1]`

Your book's options:

| Structure | Book | Cost per insert | Systems notes |
|---|---|---|---|
| `std::map` / `std::set` (red-black tree) | `[ALGO §10.2.3]` "The C++ Standard Library uses Red-Black Trees for all ordered associative containers" | O(log n), with rotations | one heap allocation per node; parent/left/right pointers + color per node `[ALGO §10.2.3]`; rotations *move* nodes around → hard to read concurrently without a lock |
| AVL tree | `[ALGO §10.1]` | O(log n), stricter balance | same concurrency problem |
| Skip list | — (Pugh) | expected O(log n), **no rebalancing** | nodes never move once linked → lock-free readers (Part 2 §13); arena-friendly |
| Sorted vector | `[ALGO §4.1]` | O(n) insert | great for *bulk* load: RocksDB's vector memtable sorts at flush time `[ROCKS-OVERVIEW]` |

RocksDB: "The default implementation of the memtable for RocksDB is a skiplist. The skiplist is a
sorted set, which is a necessary construct when the workload interleaves writes with range-scans."
`[ROCKS-OVERVIEW]`

## 4. Skip lists in depth

### The idea (Pugh) `[PUGH90]`

Start from a sorted linked list (search = O(n)). Pugh's construction:

> "If the list is stored in sorted order and every other node of the list also has a pointer to the
> node two ahead it in the list, we have to examine no more than ⌈n/2⌉ + 1 nodes … If every (2^i)th
> node has a pointer 2^i nodes ahead, the number of nodes that must be examined can be reduced to
> ⌈log₂ n⌉ while only doubling the number of pointers. This data structure could be used for fast
> searching, but insertion and deletion would be impractical."

The trick: **choose each node's level randomly** in the same proportions — 50% level 1, 25% level
2, … "Insertions or deletions would require only local modifications; the level of a node, chosen
randomly when the node is inserted, need never change." `[PUGH90]`

```
level 3: head ─────────────────────────────► 25 ──────────────► NIL
level 2: head ─────────► 9 ─────────────────► 25 ──────────────► NIL
level 1: head ─► 3 ─► 6 ─► 9 ─► 12 ─► 17 ─► 19 ─► 25 ─► 26 ─► NIL
```

**Search** (Pugh's algorithm, which LevelDB's `FindGreaterOrEqual` implements): start at the top
level of the head; move right while the next key is smaller than the target; when you can't, drop
one level; at level 1 the next node is the answer (or NIL).

LevelDB's version `[LDB-SRC:db/skiplist.h]`:

```cpp
Node* x = head_;
int level = GetMaxHeight() - 1;
while (true) {
  Node* next = x->Next(level);
  if (KeyIsAfterNode(key, next)) {
    // Keep searching in this list
    x = next;
  } else {
    if (prev != nullptr) prev[level] = x;   // remember where we dropped down (for Insert)
    if (level == 0) {
      return next;
    } else {
      // Switch to next list
      level--;
    }
  }
}
```

The `prev[]` array records, at each level, the last node before the insertion point — exactly the
nodes whose pointers Insert must update.

### Choosing p (the probability of going up a level)

Pugh's Table 1 `[PUGH90]`:

| p | normalized search time (L(n)/p) | avg pointers per node 1/(1−p) |
|---|---|---|
| 1/2 | 1 | 2 |
| 1/e | 0.94… | 1.58… |
| **1/4** | **1** | **1.33…** |
| 1/8 | 1.33… | 1.14… |
| 1/16 | 2 | 1.07… |

Pugh: "I suggest that a value of 1/4 be used for p unless the variability of running times is a
primary concern, in which case p should be 1/2." With p = 1/4 you get the same search time as p =
1/2 with one-third fewer pointers — *memory* is the systems win.

LevelDB uses exactly that `[LDB-SRC:db/skiplist.h]`:

```cpp
enum { kMaxHeight = 12 };
int SkipList<Key, Comparator>::RandomHeight() {
  // Increase height with probability 1 in kBranching
  static const unsigned int kBranching = 4;
  int height = 1;
  while (height < kMaxHeight && rnd_.OneIn(kBranching)) {
    height++;
  }
  return height;
}
```

**MaxLevel.** Pugh: "we should choose MaxLevel = L(N) (where N is an upper bound on the number of
elements in a skip list)" with L(n) = log_{1/p} n. **(derived)** With p = 1/4 and kMaxHeight = 12,
L(N) = 12 ⇒ N = 4¹² = 16 777 216 entries — comfortably more than a 4 MiB memtable can hold
(LevelDB's default `write_buffer_size = 4 * 1024 * 1024` `[LDB-SRC:include/leveldb/options.h]`).
Notice also `rnd_(0xdeadbeef)`: a **fixed seed** — deterministic behaviour makes bugs
reproducible.

### Why a skip list is the right memtable (summary, derived from Part 2)

- **No rebalancing** ⇒ a linked node never moves ⇒ readers can traverse without locks using
  acquire-loads; the writer publishes with one release-store.
- **Nodes are immutable after linking** and **never freed individually** ⇒ allocate them from an
  arena, free the whole arena after flush.
- **In-order iteration** is just walking level 0 ⇒ flushing to a sorted file is a linear scan.
- **Cost:** pointer chasing. Each hop is a likely cache miss (nodes are wherever the arena put
  them). Fine for a memtable of a few MiB; this is part of why memtables are kept small.

## 5. B-trees on disk — and why in-place updates hurt

Your book's chapter 11 is the right foil for the LSM-tree. Recap `[ALGO §11.1]`:

- A B-tree node holds up to `2t − 1` keys; "Each node should fit into a single disk block
  (commonly 4KB or 8KB)", with `2t − 1 ≈ block_size / sizeof(Key + Value + pointer)`.
- "Large fan-out reduces tree height → fewer disk accesses per operation."
- On disk, children are "disk offsets or page IDs" instead of pointers.

This is excellent for **reads**: a lookup touches height-many pages, and the top levels are
usually cached.

### The write problem (O'Neil's argument) `[ONEIL96 §1, §3]`

For random keys, each insert lands on a random leaf page. O'Neil's Example 1.2: with 576 000 000
index entries (9.2 GB) and random account IDs, "each transaction will require at least one page
read from this index, and in the steady state a page write as well" — the leaf pages are too many
to stay cached, so **every insert costs ~2 random I/Os**. For their 1000-transactions/second
benchmark that's 2000 extra random I/Os per second, "requir[ing] a purchase of an additional 50
disk arms, doubling our disk requirements". And: "there is no batching effect in a B-tree index:
each leaf node is read in, an insert of a new entry is performed, and it is written out again."

**(derived)** With Part 1's numbers: random 4 KB I/O on the Cheetah ≈ 6 ms → ~166 I/Os/s per
disk. A B-tree that needs 2 random I/Os per insert caps one disk at ~83 inserts/s. Sequential
writing at 125 MB/s could absorb ~7.8 million 16-byte entries per second. The gap is the opening
the LSM-tree exploits.

### A systems critique of `[ALGO §11.1.4]` / `[ALGO §11.2]`

The book's prototype is for learning B-tree *logic*. As a *storage* design it has issues you now
have the vocabulary to name — not to dunk on the book, but because spotting these is the job:

| Book code / claim | Problem | Source of the rule |
|---|---|---|
| `file.write(reinterpret_cast<const char*>(&node), sizeof(node))` | writes padding (5 garbage bytes in my measurement) and native byte order | Part 2 §3–§4 |
| `file.seekp(offset)` then overwrite a node | **in-place update**: a 4 KB node spans 8 sectors → torn write on power loss | `[OSTEP-37]` |
| split writes a new node, updates the child, updates the parent | three separate page writes that must *all* land → crash between them = corrupt tree (the crash-consistency problem) | `[OSTEP-42]` |
| no checksum per node | silent corruption returns wrong data | `[OSTEP-45]` |
| no `flush`/`fsync` | data may still be in the page cache when the program "finishes" | `[MAN-write]`, `[MAN-fsync]` |
| `int keys[MAX_KEYS]` fixed-size keys | real keys are variable-length byte strings → need length-prefixed encoding | Part 2 §6 |

Real B-tree databases fix these with a write-ahead log, page checksums, and careful write
ordering — the book itself lists "Add transaction support or journaling for crash safety" under
extensions `[ALGO §11.2.5]`. The LSM-tree sidesteps the *in-place* problems entirely: "Newly merged
blocks are written to new disk positions, so that the old blocks will not be overwritten and will
be available for recovery in case of a crash." `[ONEIL96 §2]`

> **In-place vs out-of-place** — the survey's framing `[LUO20 §2.1]`: "An in-place update
> structure, such as a B+-tree, directly overwrites old records … These structures are often
> read-optimized … However, this design sacrifices write performance, as updates incur random
> I/Os." "In contrast, an out-of-place update structure, such as an LSM-tree, always stores
> updates into new locations … This design improves write performance since it can exploit
> sequential I/Os … It can also simplify the recovery process by not overwriting old data.
> However, the major problem of this design is that read performance is sacrificed since a record
> may be stored in any of multiple locations."

Note: the LSM doesn't abandon B-trees. Each SSTable's *index block* is a (one-level) B-tree-ish
directory of separator keys → block offsets; O'Neil's C1 "has a comparable directory structure to
a B-tree, but is optimized for sequential disk access, with nodes 100% full" `[ONEIL96 §2]`.
**Immutable, 100%-full, bulk-built** B-trees are cheap; *mutable* ones are what hurt.

## 6. Merging

### Two-way merge

Your book's `merge` step of merge sort `[ALGO §17.1.1]`:

```cpp
while (i < n1 && j < n2) {
    if (L[i] <= R[j]) arr[k++] = L[i++];
    else arr[k++] = R[j++];
}
```

and it notes "Stable Sorting: Merge Sort preserves the relative order of equal elements." —
because `<=` takes from the left run on ties.

### Merging *versions*: "newest wins"

In an LSM-tree, equal keys in two runs are **two versions of the same key**. The merge must keep
only the newest. **(derived)** If you always put the *newer* run on the "left" and use a stable
merge, the first occurrence of each key in the output is the newest; skip the following
duplicates. LevelDB makes this explicit by attaching a **sequence number** to every entry and
ordering equal user keys by *decreasing* sequence number (Part 5 §4) — so "first one seen wins"
is guaranteed by the sort order itself.

### k-way merge

Compaction and range scans merge *many* runs (memtable + several L0 files + one file per level).
The survey: "A range query can search all components at the same time, feeding the search results
into a priority queue to perform reconciliation." `[LUO20 §2.2.1]`

With k runs, a min-heap (`std::priority_queue` with a reversed comparator, `[ALGO §6.1.5]`) of the
current head of each run gives the next smallest key in O(log k) per element **(derived)**.

LevelDB actually uses a **linear scan** over the children: "We might want to use a heap in case
there are lots of children. For now we use a simple array since we expect a very small number of
children in leveldb." `[LDB-SRC:table/merger.cc]`, and its design doc lists "the O(N) complexity in
the merging iterator" as a known cost when L0 has many files `[LDB-IMPL]`. **Lesson:** for small k,
a linear scan over a contiguous array beats a heap (fewer branches, cache-friendly) — a classic
"constant factors matter" call your book discusses in `[ALGO §25.2]`.

## 7. Bloom filters

### Why the LSM needs them

A point lookup for a key that *isn't* in a file would otherwise cost a block read per file. The
survey: "For a zero-result point lookup … all disk I/Os are caused by Bloom filter false
positives" `[LUO20 §2.3]` — i.e. with filters, a miss costs (almost) nothing.

### How they work `[LUO20 §2.2.2]`

> "To insert a key, it applies multiple hash functions to map the key into multiple locations in a
> bit vector and sets the bits at these locations to 1. To check the existence of a given key, the
> key is again hashed to multiple locations. If all of the bits are 1, then the Bloom filter reports
> that the key probably exists. By design, the Bloom filter can report false positives but not
> false negatives."

> "The false positive rate of a Bloom filter can be computed as (1 − e^(−kn/m))^k, where k is the
> number of hash functions, n is the number of keys, and m is the total number of bits … the optimal
> number of hash functions … is k = (m/n) ln 2. In practice, most systems typically use 10
> bits/key as a default configuration, which gives a 1% false positive rate."

Plugging in LevelDB's choice of k (below) **(derived)**:

| bits/key (m/n) | k = ⌊0.69·m/n⌋ | FP = (1 − e^(−k·n/m))^k |
|---|---|---|
| 5 | 3 | 9.19 % |
| 10 | 6 | 0.84 % |
| 16 | 11 | 0.046 % |

RocksDB's guidance on diminishing returns `[ROCKS-BLOOM]`: "9.9 bits per key (1% false positive
rate) is 99% as effective as 100 bits per key".

### The non-negotiable property: the hash must be **deterministic**

The filter is built when the SSTable is written and queried *later* — possibly after a restart,
possibly on another machine. **(derived)** So the bit positions for a key must be a pure function
of the key bytes and the filter's own parameters. Nothing else.

### Experiment: the `[ALGO §20.3.2]` sketch

The book's class:

```cpp
vector<size_t> hashIndices(const string& s) {
    vector<size_t> indices;
    hash<string> hasher;
    for (int i = 0; i < numHashes; ++i) {
        size_t h = hasher(s) ^ (rng() + i*0x9e3779b9);   // rng() advances every call!
        indices.push_back(h % size);
    }
    return indices;
}
```

`rng` is a `mt19937` member seeded from `random_device`; every call to `rng()` returns a new
number. So `insert("apple")` and `contains("apple")` probe **different** bit positions. I inserted
"apple" into a fresh filter and queried it, 1000 times **(measured)**:

```
false negatives for an inserted key: 1000 / 1000
```

A Bloom filter's one promise — *no false negatives* — is broken every time; the book's comment
`cout << bf.contains("apple") // true` is not what the code does. (The book's intent — "Randomization
enters in hash function selection" — is legitimate if the random *seeds* are drawn once at
construction and stored.)

A second, subtler issue for on-disk use: `std::hash` "only required to produce the same result for
the same input within a single execution of a program; this allows salted hashes"
`[CPPREF-hash]`. **(derived)** A filter built with `std::hash` and saved to disk may be garbage
after a restart or a library upgrade. Persistent filters need a **specified** hash function.

### A correct design: LevelDB's `BloomFilterPolicy` `[LDB-SRC:util/bloom.cc]`

```cpp
explicit BloomFilterPolicy(int bits_per_key) : bits_per_key_(bits_per_key) {
  // We intentionally round down to reduce probing cost a little bit
  k_ = static_cast<size_t>(bits_per_key * 0.69);  // 0.69 =~ ln(2)
  if (k_ < 1) k_ = 1;
  if (k_ > 30) k_ = 30;
}

void CreateFilter(const Slice* keys, int n, std::string* dst) const override {
  size_t bits = n * bits_per_key_;
  // For small n, we can see a very high false positive rate.  Fix it
  // by enforcing a minimum bloom filter length.
  if (bits < 64) bits = 64;
  size_t bytes = (bits + 7) / 8;
  bits = bytes * 8;
  const size_t init_size = dst->size();
  dst->resize(init_size + bytes, 0);
  dst->push_back(static_cast<char>(k_));  // Remember # of probes in filter
  char* array = &(*dst)[init_size];
  for (int i = 0; i < n; i++) {
    // Use double-hashing to generate a sequence of hash values.
    // See analysis in [Kirsch,Mitzenmacher 2006].
    uint32_t h = BloomHash(keys[i]);
    const uint32_t delta = (h >> 17) | (h << 15);  // Rotate right 17 bits
    for (size_t j = 0; j < k_; j++) {
      const uint32_t bitpos = h % bits;
      array[bitpos / 8] |= (1 << (bitpos % 8));
      h += delta;
    }
  }
}
```

Systems details worth stealing:

- **One hash, k probes** ("double hashing"): compute one 32-bit hash, derive the others by adding
  a rotated copy. k probes cost one hash computation.
- **k is stored in the filter** (`push_back(k_)`), and the reader uses "the encoded k so that we
  can read filters generated by bloom filters created using different parameters" — the file is
  **self-describing**, so you can change the default later without breaking old files.
- **Minimum 64 bits** to avoid terrible FP rates for tiny files.
- The hash (`Hash(data, n, 0xbc9f1d34)` in `util/hash.cc`) is a fixed algorithm with a fixed seed.

My deterministic re-implementation (FNV-1a hash + LevelDB's double hashing, 100 000 keys,
10 bits/key) **(measured)**:

```
k=6 bits=1000000 false negatives=0 false positive rate=1.449%
```

Zero false negatives ✔. FP 1.45% vs 0.84% predicted — my hypothesis is weak hash mixing on
sequential keys ("key0", "key1", …) with only a 32-bit hash; RocksDB documents that its *first*
full-filter implementation "could not get an FP rate better than about 0.1%" and had "inherent
limitations of 32-bit hashing", fixed with "a 64-bit hash" `[ROCKS-BLOOM]`. **Hash quality is a
real engineering parameter.** (Drill 4 asks you to test the hypothesis.)

Follow-up **(measured)** while validating Exercise 1: a 64-bit FNV-1a with a final bit-mixing step,
on *random* 16-byte keys, 20 000 keys at 10 bits/key, gave **0.877%** — right on the formula's 0.84%.
So the gap above came from the hash/key combination, not from the double-hashing scheme.

### Where filters live

LevelDB stores filters *inside the SSTable* as a "filter" meta block, one filter per 2 KB range of
data-block offsets `[LDB-TABLE]`. RocksDB's newer "full filter" is one per file, with each key's
probes confined to one cache line `[ROCKS-BLOOM]`. Byte layout: Part 6.

## 8. Checksums: CRC32C and the "masked CRC"

A CRC is an error-detecting code; LevelDB uses the CRC32C variant on WAL records ("checksum:
uint32 // crc32c of type and data[]" `[LDB-LOG]`) and on every table block (trailer = "1-byte type +
32-bit crc" `[LDB-SRC:table/format.h]`).

One subtle trick, `[LDB-SRC:util/crc32c.h]`:

```cpp
// Motivation: it is problematic to compute the CRC of a string that
// contains embedded CRCs.  Therefore we recommend that CRCs stored
// somewhere (e.g., in files) should be masked before being stored.
inline uint32_t Mask(uint32_t crc) {
  // Rotate right by 15 bits and add a constant.
  return ((crc >> 15) | (crc << 17)) + kMaskDelta;
}
```

**(derived)** A WAL may contain records whose payload is itself a WAL fragment (e.g. a MANIFEST
written in log format, copied around). Masking makes a stored CRC look unlike a raw CRC so that
data containing CRCs doesn't accidentally "validate". `[LDB-LOG]` mentions a related benefit: "we do
not get confused when part of the contents of one log file are embedded as a record inside
another log file."

## 9. Summary

| Block | Role in the LSM | Key systems property |
|---|---|---|
| Sorted run | every SSTable / level | enables binary search, range scans, linear merge |
| Comparator | defines order of all files | must be total, stable forever, byte-level |
| Skip list | memtable | no rebalancing → lock-free reads; arena-allocated |
| B-tree (immutable) | SSTable index | bulk-built, 100% full; mutable B-trees cost random I/O per write |
| k-way merge | compaction, scans, reads | "newest wins" via ordering; linear scan fine for small k |
| Bloom filter | skip files on lookups | deterministic, specified hash; self-describing params |
| CRC32C | every record/block | detects accidents, not attacks; mask stored CRCs |

## Drills

1. Implement Pugh's skip list with `p = 1/4`, `MaxLevel = 12`, a fixed seed, and `std::map` as a
   reference oracle. Insert 1M random keys; compare iteration output. Then count average pointers
   per node and compare with Pugh's 1.33.
2. Fix `[ALGO §20.3.2]` with the smallest possible change (hint: draw the seeds once). Then make
   it *persistable*: replace `std::hash` with a specified hash and store `k` in the filter bytes.
3. Implement a k-way merge of `k` sorted `std::vector<std::pair<std::string,uint64_t>>` (key,
   seq) where equal keys keep only the highest seq. Benchmark heap vs linear scan for k = 2, 4, 8,
   32, 128. Where's the crossover on your machine?
4. Test the Bloom hypothesis: rerun the measurement with random 16-byte keys instead of "keyN",
   and with a 64-bit hash. Does FP approach 0.84%?
5. B-tree crash thought experiment: list every on-disk write a B-tree insert performs when it
   splits a leaf *and* the root. For each prefix of those writes landing before a crash, what does
   a reader see?

## My summary

_(write 5 lines here)_
