# 17.3 — Safe-linking and metadata protection

Canaries (§17.2) catch an overflow that *crosses* them. But the highest-value target is the free-list
**pointer** itself (Lesson 7.5's fastbin/tcache poisoning): overwrite a freed chunk's "next" and the
allocator hands you an arbitrary address. Safe-linking makes that pointer unforgeable.

## 1. The attack it defeats

Recap (Lesson 7.5, 17.1 §2): a free chunk stores the next free chunk's address in its payload (the
intrusive free list, Lesson 7.2 §2 / 10.3). An attacker who can write into a freed chunk (UAF-write) or
overflow into it replaces that stored pointer with a chosen target `T`. The next two `malloc`s return
the real chunk and then **`T`** — now writing to that "allocation" writes wherever the attacker aimed.
This single technique (fastbin dup / tcache poisoning) is behind a large fraction of modern heap
exploits, precisely because the stored pointer is raw and trusted.

## 2. Safe-linking: XOR the pointer with where it lives

glibc 2.32 introduced **safe-linking** `[SAFELINK]`. Instead of storing the raw next pointer `P`, it
stores `P ⊕ (L >> 12)`, where `L` is the **address of the location holding the pointer** (the slot).
On unlink it XORs back with `(L >> 12)` to recover `P`. Two consequences:

- **The stored value is not a usable pointer.** An attacker who *leaks* the free-list word sees
  `P ⊕ (L>>12)`, not `P` — useless without knowing `L>>12` (which depends on ASLR'd addresses).
- **An attacker who *overwrites* the word can't choose the decoded result** without knowing `L>>12`:
  to make the chunk decode to target `T`, they must write `T ⊕ (L>>12)`, i.e. they must already know
  the secret location bits. A naïve overwrite decodes to garbage.

The `>>12` (a page shift) mixes in the high, ASLR-randomized bits of the slot's own address while
staying stable within a page, so it's cheap and needs no stored secret — the randomness comes from
where the heap landed. (Your exercise uses the same `⊕ (slot >> 12)` transform; a per-allocator random
`secret_` could be mixed in too, as some allocators do.)

### Alignment check: the second half of safe-linking
Safe-linking pairs with an **alignment check**: after decoding, verify the pointer is suitably aligned
(and, in a bounded allocator, in-range). A forged or garbage-decoded pointer is almost never correctly
aligned → the allocator rejects it and aborts, instead of returning it. `[SAFELINK]` added exactly this
("a check verifies the alignment of the returned chunk"). Your exercise's provided `allocate` does the
in-range check on the decoded link, and the exercise's `corrupted_free_list_link_is_rejected` test
proves a garbage link is caught.

## 3. The general principle: don't trust inline metadata

Safe-linking is one instance of a broader hardening idea — **make the allocator distrust its own
in-band metadata**, because that metadata is reachable by the bugs it's defending against:
- **Masked pointers** (safe-linking) — a leaked/overwritten pointer is useless.
- **Size/sanity checks** — glibc checks "corrupted size vs. prev_size," "invalid next size,"
  "corrupted chunk" on the hot path; a forged size that would overlap chunks is rejected. (The boundary
  tags of Lesson 7.2 are a *check* surface as much as a navigation aid.)
- **Out-of-line metadata** — the strongest version: keep the bookkeeping in a *separate* region the
  user's overflow can't reach at all (the detector pattern of Lesson 16.1 §2). hardened_malloc
  `[HARDENED-MALLOC]` and PartitionAlloc `[PARTITIONALLOC]` do this — an overflow of user data simply
  can't corrupt a header, because there is no header adjacent to the data. This removes the
  metadata-corruption primitive *by construction* (not just detects it), at the cost of a pointer→
  metadata lookup.

## 4. Where your exercise sits
The exercise keeps metadata *inline* (to teach the classic layout and the attacks on it) but **hardens
it**: safe-linked free-list pointers + alignment/in-range checks + canaries (§17.2). That's the glibc
2.26–2.32 hardening applied to your own allocator. The out-of-line-metadata step (hardened_malloc) is
the next level and a natural extension / drill — and the design your capstone detector uses (its
records live out of line, with guard pages, §17.4).

## Drills
1. Implement safe-linking in the exercise (`encode_link`/`decode_link` = `⊕ (slot>>12)`). Show the
   `safe_linking_obfuscates_the_pointer` test now passes, and explain why the stored value ≠ the raw
   pointer when `slot>>12 ≠ 0`.
2. An attacker wants `malloc` to return target `T` via a freed chunk at slot `L`. Exactly what must they
   write into the link, and what must they already know? Why does that usually require an info leak now?
3. Why does an alignment check catch a forged link even when the attacker *can* write the word? Construct
   a forged value that decodes to an unaligned address and trace the rejection.
4. Compare inline-but-masked metadata (safe-linking) with out-of-line metadata (hardened_malloc, §3):
   which *detects* corruption and which makes it *impossible*? What does each cost?

## My summary
