# 15.3 — The ABA problem

## 1. The definition

ABA is the subtlest bug in lock-free programming, and it is a *consequence* of reusing freed memory
(§15.2). `[MCCP §12]` defines it:

> "The ABA problem occurs when a thread observes a memory location in the following sequence:
> - Value is read as A.
> - Value changes to B by another thread.
> - Value changes back to A.
>
> From the observing thread's perspective, nothing appears to have changed, even though the underlying
> state may be fundamentally different." `[MCCP §12]`

And why that is catastrophic for lock-free code `[MCCP §12]`:

> "`compare_exchange` validates **equality, not history**. … Structural invariants may be silently
> violated." `[MCCP §12]`

A CAS says "if the value is *still* what I read, swap it." ABA fools exactly that check: the value is
"still" A by bit-equality, so the CAS succeeds — but it's a *different* A.

## 2. ABA on a Treiber-stack pointer

`[MCCP §12]`'s canonical example is a stack `pop` whose `head` CAS is defeated by node reuse:

```cpp
std::atomic<Node*> head;
void pop() {
    Node* old_head = head.load(std::memory_order_acquire);
    if (!old_head) return;
    Node* next = old_head->next;                 // T1 reads A's next
    head.compare_exchange_strong(old_head, next, /*...*/);   // "CAS succeeds even if old_head was removed and reinserted"
}
```

The failure sequence `[MCCP §12]`:

> - "Thread T1 reads `old_head = A`.
> - Thread T2 removes node A, frees it, and allocates a new node at the same address.
> - The address stored in `head` becomes A again.
> - T1's CAS succeeds, despite operating on a logically different node." `[MCCP §12]`

Concretely: T1 read `next = A->next` pointing at the *old* second node, say X. While T1 is stalled, T2
pops A (head→B), pops B, frees both, then pushes a *new* node that the allocator happens to place at
address A again (allocators reuse addresses — Lesson 7.3 fastbins/tcache do this *by design*). Now
`head == A` again. T1's CAS(`head`, expected A, desired X) **succeeds** — and sets `head` to X, a node
that was already popped and freed. The stack is corrupted; X is a use-after-free. `[MCCP §12]`: "The
atomic operation is correct, but the algorithm is not."

The crucial link: **ABA is enabled by reclamation** (§15.2). If A were never freed-and-reused, its
address couldn't come back. Address reuse (the thing allocators do for speed) is what turns a benign
"value changed back" into corruption. ABA and reclamation are two faces of the same coin.

## 3. ABA is not a data race

`[MCCP §12]`: "The ABA problem is **not** a data race. It is a logical correctness failure caused by
insufficient state representation." So **ThreadSanitizer will not catch it** — every access was
properly atomic and ordered. ABA is a design bug, found by reasoning, not by TSan. (This is why §15.4's
schemes and the exercise's sequence-number design matter: you engineer ABA out, you don't sanitize it
away.)

## 4. The fixes: add history to the value

Since CAS checks equality and ABA abuses equality, the fix is to make "equal" imply "unchanged" by
**encoding history into the value** `[MCCP §12]`:

- **Tagged / versioned pointers (counter).** Pack a monotonically-increasing **version counter**
  alongside the pointer and CAS the *pair*. A→B→A now reads A|v1 then A|v3 — not equal, CAS fails,
  correctly. Needs a double-width CAS (e.g. `cmpxchg16b`) or spare pointer bits. "tagging the value
  with a counter (version, epoch, or stamp)" `[MCCP §12]`.
- **Don't reuse the address while anyone might hold it** — i.e. a reclamation scheme (hazard
  pointers/epochs, §15.4): if A can't be freed until no thread references it, A's address can't come
  back mid-operation, and ABA can't occur. Reclamation and ABA are solved *together*.
- **Avoid pointers/reuse entirely** — a bounded array queue with per-cell **sequence numbers** (§15.4,
  the exercise): each cell's sequence advances every lap, so the "same slot" is never mistaken for the
  "same state." This is tagging applied to array indices, and it's why the exercise is ABA-free by
  construction.

## Drills
1. Walk the §2 sequence step by step with concrete addresses (A, B, X) and show the exact moment T1's
   CAS corrupts the stack. Which node becomes a use-after-free, and why couldn't it happen if A were
   never freed?
2. Why does a version counter (CAS the `{pointer, version}` pair) defeat ABA while CASing the pointer
   alone does not? What hardware does the double-width CAS need?
3. Explain `[MCCP §12]`'s claim that ABA is "not a data race." Why can't ThreadSanitizer find it, and
   what *would* find it (review? a model checker? a stress test with an adversarial allocator)?
4. Preview: the exercise's MPMC cell has a `seq` that increases by `Cap` each full lap. Argue
   informally why a producer can never mistake a stale cell for a ready one (ABA-free).

## My summary
