# Decisions (one line each: decision — why — source)

## UniquePtr move = steal + null source (why copy is =delete, one owner only — 12.1 §3)

## SharedPtr copy ctor = ++strong; move = steal (no count change) (12.2 §3)

## SharedPtr copy assign via copy-and-swap (strong guarantee + self-safe — 11.4 §2 / 12.2)

## control block: strong vs weak counts; who frees the object vs the block (12.2–12.3)

## the self weak-ref (weak starts at 1) — why it makes block deletion reentrancy-safe (12.2 note)

## WeakPtr::lock uses incref_if_nonzero (why a test-then-use would race — 12.3 §4)

## expired() == (strong count 0); weak never changes the strong count (12.3)

## RESULTS/observations: use_count walk, cycle-count demo, lock before/after expiry (notes 12.x)
