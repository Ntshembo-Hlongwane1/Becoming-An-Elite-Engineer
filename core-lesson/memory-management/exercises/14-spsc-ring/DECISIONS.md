# Decisions (one line each: decision — why — source)

## which loads get acquire (the ones reading the OTHER thread's index) — why (14.4 §2 / 14.3 §4)

## which stores get release (publish a slot / free a slot) — the happens-before they create (14.3)

## why reading your OWN index is fine with relaxed (no cross-thread publish through it) (14.3 §5)

## one-empty-slot convention: full == next(tail)==head; usable capacity Cap-1 (14.4 §1)

## alignas(64) on head_/tail_ — false sharing between producer and consumer (Lesson 6.4 / 14.4 §1)

## why SPSC needs no CAS (single writer per index) and the MP case would (ABA) (14.4 §3/§5)

## evidence: two-thread test correct AND TSan-clean; the relaxed stub's race (notes 14.4 §4)
