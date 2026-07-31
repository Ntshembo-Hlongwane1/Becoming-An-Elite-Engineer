# 04 — Posting Lists, Deep

> We open a segment's postings file and go to the **bit level**. What a posting *is*; why
> postings are stored sorted; **delta (gap) encoding**; **variable-byte** and
> **Frame-of-Reference / PForDelta** block packing; **skip lists** for fast intersection;
> and the per-block **impacts** (max scores) that make **BlockMax WAND** possible in doc 06.
> Every technique is worked byte-by-byte, with best-practice C++ encode/decode. This is the
> densest doc; read it with doc 01 (memory hierarchy) in mind — every trick here is "move
> fewer, closer bytes."

---

## 1. What is a posting?

The inverted index maps **term → posting list**. A **posting** is one entry: "term T occurs
in document D." Depending on what queries you support, a posting carries more:

```
 posting = { docID,  termFreq,  [positions...],  [offsets...],  [payload] }
             │        │           │                │              │
             always   for scoring for phrase/      for highlight  arbitrary
                      (BM25 tf)   proximity        (char spans)   per-position data
```

Lucene splits these into **separate streams/files** (doc 02 §1, temperature): `.doc` holds
`(docID, freq)`, `.pos` holds positions, `.pay` holds offsets/payloads. A pure boolean/BM25
query never touches `.pos`, so it never pages those bytes in. Separation = you only pay for
what you query.

For your inventory engine, start with `(docID, freq)` only. Positions matter when you add
phrase search ("simple truth bread" as a phrase, not three loose terms).

---

## 2. Why postings are sorted by docID — the enabling decision

A term's postings are stored **in increasing docID order**. This one choice unlocks
everything downstream:

- **Intersection/union become a merge** of sorted lists (doc 05) — linear, sequential,
  prefetch-friendly (doc 01 §2).
- **Sorted increasing integers compress spectacularly** via delta encoding (§3).
- **Skip lists** (§6) can exist, because "advance to docID ≥ target" is meaningful on a
  sorted sequence.

```
 term "milk" → docIDs:  [ 3, 8, 9, 15, 150, 152, 1000 ]   (sorted, ascending)
```

Everything in this doc assumes this ordering. If postings were unsorted you'd lose delta
compression *and* fast intersection in one stroke.

---

## 3. Delta (gap) encoding — turn big numbers into small ones

Store **differences between consecutive docIDs**, not the docIDs themselves:

```
 docIDs :  3   8   9   15   150   152   1000
 deltas :  3   5   1    6   135     2    848        (each = current − previous)
```

Why this is a win: the raw docIDs grow unboundedly (millions), needing many bits each. The
**deltas are small** (especially for common terms whose postings are dense — deltas near 1).
And **small numbers compress well** under the variable-width schemes in §4–§5. Decoding is a
running prefix sum:

```cpp
// decode deltas back to docIDs (prefix sum). deltas: input, out: docIDs.
void undelta(const std::vector<uint32_t>& deltas, std::vector<uint32_t>& out) {
    uint32_t acc = 0;
    out.resize(deltas.size());
    for (size_t i = 0; i < deltas.size(); ++i) {
        acc += deltas[i];                 // running sum reconstructs the original
        out[i] = acc;
    }
}
```

> **Note:** deltas are ≥ 1 for strictly-increasing docIDs, so some codecs store `delta − 1`
> to squeeze one more bit. Small details like this are where real formats win their last
> 10%. Don't start there; know it exists.

---

## 4. Variable-byte (VByte / varint) — the gateway compression

The simplest useful integer codec, and the one to implement first. Use **7 bits per byte for
data**; the 8th (high) bit is a **continuation flag**: 1 = "more bytes follow," 0 = "last
byte."

Worked example — encode **135** and **300**:

```
 135  = 0b1000_0111
   split into 7-bit groups (low first): 0000001 0000111
   → byte0 = 0000111 with continuation → 1000_0111 = 0x87
   → byte1 = 0000001, last            → 0000_0001 = 0x01
   135 → [0x87, 0x01]          (2 bytes)

 300  = 0b1_0010_1100
   7-bit groups: 0000010 0101100
   → byte0 = 0101100 | cont → 1010_1100 = 0xAC
   → byte1 = 0000010, last   → 0000_0010 = 0x02
   300 → [0xAC, 0x02]          (2 bytes)
```

Small numbers (0–127) take **1 byte**; that's why delta encoding (§3) pairs with it — most
deltas are tiny. Best-practice encode/decode:

```cpp
#include <cstdint>
#include <vector>

// Append LEB128-style varint. Best practice: unsigned only, explicit widths.
inline void putVarint(std::vector<std::uint8_t>& out, std::uint32_t v) noexcept {
    while (v >= 0x80) {                        // while ≥ 7 bits remain
        out.push_back(static_cast<std::uint8_t>(v) | 0x80); // low 7 bits + continuation
        v >>= 7;
    }
    out.push_back(static_cast<std::uint8_t>(v)); // final byte, high bit clear
}

// Read one varint starting at `pos`; advances `pos`. Returns the value.
inline std::uint32_t getVarint(const std::uint8_t* data, std::size_t& pos) noexcept {
    std::uint32_t result = 0;
    int shift = 0;
    std::uint8_t byte;
    do {
        byte = data[pos++];
        result |= static_cast<std::uint32_t>(byte & 0x7F) << shift; // strip continuation
        shift += 7;
    } while (byte & 0x80);                     // continue while high bit set
    return result;
}
```

- **Pros:** trivial, byte-aligned (no bit juggling), decent ratio on small ints.
- **Cons:** **branch per byte** (that `while`) — branch mispredictions hurt, and it doesn't
  vectorize well. Frontier engines moved to *block* schemes (§5) for exactly this reason:
  decode 128 ints with no per-value branching.

> **Your turn:** implement `putVarint`/`getVarint`, then encode the delta stream from §3 and
> confirm you decode the original docIDs back. This is the smallest real piece of a search
> engine you can build and test in isolation. Verify round-trip on edge cases: 0, 127, 128,
> 2^32−1.

---

## 5. Block encoding: FOR and PForDelta — how the pros pack postings

Modern Lucene doesn't VByte one posting at a time. It packs postings in **fixed-size blocks
of 128 docs** and encodes each block together. Two ideas:

### Frame of Reference (FOR)

Within a block, find the **max delta**; every value fits in
`bitWidth = ceil(log2(maxDelta + 1))` bits. Store `bitWidth` once, then **bit-pack** all 128
values at that fixed width. No continuation bits, no branches — pure bit-packing.

Worked example — block of deltas `[3, 5, 1, 6, 2, 4]`:

```
 max delta = 6 = 0b110  → needs 3 bits → bitWidth = 3
 pack each in 3 bits:  3=011  5=101  1=001  6=110  2=010  4=100
 bitstream: 011 101 001 110 010 100  → 18 bits → 3 bytes  (+1 byte header for bitWidth)
 vs VByte: 6 bytes. FOR: ~3 bytes. And decode is branchless + SIMD-able.
```

Best-practice fixed-width bit-packer (the primitive under FOR — study the masking/shifting):

```cpp
#include <cstdint>
#include <vector>
#include <cstddef>

// Pack `count` values, each using exactly `bits` low bits, LSB-first into a byte buffer.
// Precondition: every value < (1u << bits), 1 <= bits <= 32.
void bitpack(const std::uint32_t* vals, std::size_t count, unsigned bits,
             std::vector<std::uint8_t>& out) {
    std::uint64_t acc = 0;      // staging area
    unsigned filled = 0;        // bits currently in acc
    for (std::size_t i = 0; i < count; ++i) {
        acc |= static_cast<std::uint64_t>(vals[i]) << filled;
        filled += bits;
        while (filled >= 8) {                 // drain whole bytes
            out.push_back(static_cast<std::uint8_t>(acc & 0xFF));
            acc >>= 8;
            filled -= 8;
        }
    }
    if (filled > 0) out.push_back(static_cast<std::uint8_t>(acc & 0xFF)); // tail bits
}
```

(The unpacker is the mirror: pull bytes into `acc`, peel `bits` at a time with
`acc & ((1u<<bits)-1)`. Real Lucene has 32 hand-unrolled, SIMD-friendly routines — one per
bit width — because branchless fixed-width unpacking is the hot loop of the whole engine.)

### PForDelta (Patched FOR) — don't let one outlier ruin the block

FOR's weakness: **one** large delta forces a wide `bitWidth` for **all** 128 values. Example:
`[1,1,2,1,1, ... , 900000]` — that single 900000 pushes bitWidth to 20, wasting 20 bits on
every tiny value.

**PForDelta** fixes it: pick a bitWidth that fits, say, **90%** of values; store the rare
**exceptions** ("patches") separately (their positions + full values in a side array).
Decode = unpack at the narrow width, then patch the exception slots.

```
 values:  [1, 1, 2, 1, 1, 900000, 1, 2]     (one outlier)
 FOR      → bitWidth 20 for ALL 8 values  (wasteful)
 PForDelta→ bitWidth 2 for the 7 small ones; 900000 stored as an exception (pos=5)
          → tiny block + a 1-entry patch list
```

- **Why it matters:** real posting deltas are *mostly* small with occasional jumps (a gap to
  a far-away doc). PForDelta keeps the common case dense and quarantines outliers. This is
  the workhorse behind Lucene's postings.
- **Decode stays branchless** for the bulk; only the few patches need handling.

> **Build order for you:** VByte (§4) first — correct and simple. Then FOR bit-packing (the
> `bitpack` primitive above). PForDelta is a stretch — implement it only once FOR works and
> you want the last big compression/speed win. Don't skip straight to PForDelta; you'll be
> debugging bit-packing and patch logic simultaneously.

---

## 6. Skip lists — reading fewer blocks during intersection

Intersection (doc 05) repeatedly asks a posting list: **`advance(target)`** — "give me your
first docID ≥ target." Without help, that means decoding every block from the current
position forward until you pass `target` — potentially reading megabytes to skip them.

A **skip list** is sparse metadata layered over the blocks: at intervals it records
`(maxDocID_of_block, byteOffset_of_block)`. To `advance(target)`, you binary-search the skip
entries to find the block that *could* contain `target`, `seek` straight to its byte offset,
and decode only that block.

```
 blocks:   [ b0: ..300 ] [ b1: ..900 ] [ b2: ..2000 ] [ b3: ..9000 ] ...
 skip:      (300,off0)    (900,off1)     (2000,off2)    (9000,off3)
 advance(1500): binary-search skip → first maxDocID ≥ 1500 is 2000 → jump to off2,
                decode ONLY b2. Blocks b0,b1 never touched (never paged in). ← doc 01 §2 win
```

- Lucene builds **multi-level** skip lists (skips over skips) so `advance` is
  ~logarithmic in list length, not linear.
- The payoff is pure memory hierarchy: **you skip *pages you never read*.** For a
  conjunction where one term is rare and one is common, the common list gets `advance`d in
  huge jumps, touching a handful of blocks instead of all of them.

> **This is why postings are *blocked*:** blocks are the unit you skip. You can't skip into
> the middle of a variable-byte stream (you don't know where a value starts), but you *can*
> jump to a block boundary. Blocking enables skipping enables cheap intersection. Compression
> scheme and access algorithm are co-designed.

---

## 7. Impacts / BlockMax — per-block max scores (the seed of doc 06)

Store, **per block**, not just `maxDocID` but the block's **maximum score contribution**
(its max impact — derived from the max term frequency in that block, combined with the
term's idf/norms). Call it `maxScore(block)`.

```
 block:   [ b0 ]        [ b1 ]        [ b2 ]        [ b3 ]
 maxDoc:   300           900           2000          9000
 maxScore: 2.1           0.4           5.7           1.2      ← NEW: upper bound per block
```

Why this exists: top-K retrieval (doc 06) keeps a **threshold** = the current K-th best
score. If a whole block's `maxScore` is **below** the threshold, *no document in that block
can enter the top-K*, so the engine **skips the entire block without scoring any of it.**
That's **BlockMax WAND** — and it's only possible because this per-block bound was written at
*index* time, here in the postings. Doc 06 turns this bound into the pruning algorithm; just
plant the seed now: **the postings file stores upper bounds so the query can skip work.**

---

## 8. The full picture of one term's postings

Assembling §1–§7, a term's `.doc` entry looks like:

```
 term "milk" → postings:
   ┌────────── skip list (multi-level): (maxDoc, maxScore, byteOffset) per block ─────────┐
   │  (300, 2.1, off0)  (900, 0.4, off1)  (2000, 5.7, off2) ...                            │
   └───────────────────────────────────────────────────────────────────────────────────┘
   block b0: [bitWidth=3][packed deltas of 128 docIDs][packed freqs] (FOR/PForDelta)
   block b1: [bitWidth=..][packed deltas][packed freqs]
   ...
```

- **docIDs**: delta-encoded, block-packed (FOR/PForDelta), sorted ascending.
- **freqs**: packed alongside (for BM25 tf).
- **skip list**: sparse `(maxDoc, maxScore, offset)` for `advance` + block-skip pruning.
- **positions/offsets**: separate `.pos`/`.pay` streams, only if you index them.

Every property serves the memory hierarchy: sorted+delta+packed = few bytes moved;
blocked = skippable; per-block bounds = whole blocks skipped during ranking.

---

## 9. Mapping to your engine

Your current "postings" are `std::vector<std::string> docs` per term in an
`unordered_map` — uncompressed, unsorted, string-valued, RAM-only. The upgrade path:

1. **Assign integer docIDs.** Map each product file → a dense `uint32_t` docID (0,1,2,…).
   Postings become `uint32_t`, not strings. (Keep a docID→filepath table for the fetch
   phase — doc 05.) *This single change unlocks delta encoding and everything after.*
2. **Sort postings by docID**, delta-encode, **VByte** them → your first compressed on-disk
   postings file. Verify round-trip.
3. **Block them (128) + FOR bit-packing** + a single-level **skip list** of `(maxDoc,
   offset)`. Now you can `advance(target)`.
4. **Add per-block `maxScore`** once you implement BM25 (doc 06) — wire up BlockMax later.

> **Your turn:** design the on-disk byte layout for one term's postings (block header, packed
> docIDs, packed freqs, skip entries). Write it down as a byte-offset spec (doc 02 §7). Then
> implement steps 1–2 and unit-test the round-trip on your real `data/` files. That's a
> genuine, testable milestone — a compressed inverted index you built from scratch.

---

## 10. Before you move on

1. Why are postings sorted by docID, and what *three* capabilities does that unlock?
2. Encode `[3,8,9,15,150]` with delta + VByte by hand; give the byte count. Now do FOR on
   the deltas — which wins here and why?
3. What failure of FOR does PForDelta fix, and how?
4. Why must postings be *blocked* for skip lists to work? What can't you do mid-block?
5. What is stored per block to enable BlockMax WAND, and *when* (index time vs query time) is
   it computed?
6. What is the one change to your current index that unlocks compression, and why is it the
   linchpin?

Next: **05 — Query Lifecycle**, where these posting lists become `DocIdSetIterator`s and we
trace a query from raw string through analysis, rewrite, per-segment scorers, the top-K
collector, segment/shard merge, and the fetch phase — with the leapfrog-intersection and
disjunction-priority-queue algorithms in code.
