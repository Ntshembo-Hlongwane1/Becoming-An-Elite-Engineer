# Decisions (one line each: decision — why — source)

## canary secret + per-block (not a constant) — forgery/leak resistance (17.2 §1)

## header + trailer canary — catch overflow in either direction, checked at free (17.2 §1)

## safe-linking: next ⊕ (slot>>12) — a leaked/overwritten link is useless without the location (17.3 §2)

## alignment/in-range check on the decoded link — reject forged/garbage pointers (17.3 §2)

## double-free/invalid-free via ALLOC/FREE state magic (glibc tcache key) (17.2 §3)

## poison-on-free (0xDD) — clear secrets, surface UAF (17.2 §2)

## detection policy: throw/abort on corruption — deny the attacker a retry (17.2 §4)

## why ASan can't catch this (one arena allocation) so the allocator must self-check (10.5 / 17.1 §4)

## extensions: out-of-line metadata + guard pages = the capstone detector (17.3 §3 / 17.4)
