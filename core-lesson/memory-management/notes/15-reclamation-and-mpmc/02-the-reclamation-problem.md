# 15.2 — The reclamation problem

## 1. The question that defines lock-free memory management

Here is the problem that makes lock-free data structures genuinely hard — harder than getting the
atomics right. Consider a lock-free stack (a **Treiber stack**): `head` is an `atomic<Node*>`; `pop`
reads `head`, then CASes `head` to `head->next`:

```cpp
void pop() {
    Node* old = head.load(std::memory_order_acquire);
    while (old && !head.compare_exchange_weak(old, old->next)) {}
    // ... now what? can we `delete old;` ?
}
```

With **one** popping thread, you can `delete old` — nobody else touches popped nodes. With **multiple**
poppers, you cannot, and `[CIA §7.2.2]` states exactly why:

> "The basic problem is that you want to free a node, but you can't do so until you're sure there are
> no other threads that still hold pointers to it. … if you need to handle multiple threads calling
> pop() on the same stack instance, you need some way to track when it's safe to delete a node."
> `[CIA §7.2.2]`

The race: thread A loads `old = head` and is about to read `old->next`; thread B pops the *same* node
and `delete`s it; A now dereferences a **freed** `old` → use-after-free (the Lesson-7.5 primitive). The
atomics on `head` are perfectly correct; the **memory lifetime** of the node is the bug.

## 2. Why this is uniquely a lock-free problem

With a mutex you'd never see it: the popping thread holds the lock across "remove node *and* no one
else can be mid-pop," so freeing is trivially safe. Lock-free code removes the lock precisely so
threads *can* be mid-operation simultaneously — which is exactly what makes "is anyone still looking at
this node?" unanswerable without extra machinery. `[CIA §7.2.2]` calls it what it is: "you need to
write a special-purpose garbage collector just for nodes."

Note the asymmetry Williams points out: `push` is fine — a new node is visible to only one thread until
it's linked in, so no one else can be reading it. The hazard is entirely on the `pop` side, where
"multiple threads might be accessing the same node" `[CIA §7.2.2]`.

## 3. The non-solutions

- **Just leak the nodes.** Correct but unacceptable — unbounded memory growth (a DoS, Lesson 15.5).
  Williams uses leaking only as a temporary stand-in before fixing it `[CIA §7.2.2]`.
- **Just `delete` immediately.** The §1 use-after-free.
- **Reference-count the *nodes* with `shared_ptr`.** `atomic<shared_ptr<Node>>` works and is the
  easiest correct option, but the atomic refcount traffic (Lesson 12.4) is often slower than the
  lock-free structure it's protecting — you pay contention on every node.

## 4. A first real solution: count threads in pop

The simplest scheme that actually works `[CIA §7.2.2]`: don't delete a removed node immediately; put
it on a **"to be deleted" list**, and only actually free that list when you know **no thread is
currently inside `pop`**. You know that by counting:

> "if you increment a counter on entry and decrement that counter on exit, it's safe to delete the
> nodes from the 'to be deleted' list when the counter is zero. Of course, it will have to be an atomic
> counter." `[CIA §7.2.2]`

```cpp
std::atomic<unsigned> threads_in_pop;
std::shared_ptr<T> pop() {
    ++threads_in_pop;                 // "I'm reading nodes now"
    node* old = head.load();
    while (old && !head.compare_exchange_weak(old, old->next)) {}
    // ... extract data from old ...
    try_reclaim(old);                 // if threads_in_pop == 1 (just me), free the pending list;
    return ...;                       // else, stash old on the pending list for later
}
```

This is correct, but it has a weakness: under constant pop traffic the counter is *never* zero, so the
pending list grows without bound (a latent leak). That's what pushes real systems to the finer-grained
schemes of §15.4 (hazard pointers, epochs). But first, the subtle trap that reusing nodes creates: ABA
(§15.3).

## 5. The through-line
Every lock-free structure that **allocates** nodes must answer "when is it safe to free this?" The
answers form a spectrum (§15.4): ref-counting (simple, slow), hazard pointers (per-node precision),
epoch/RCU (batched, fast). Or you **avoid the question**: a bounded, array-based structure that never
frees nodes has no reclamation problem at all — which is why the exercise's MPMC queue is bounded
(§15.4).

## Drills
1. Draw the exact two-thread interleaving (A loads `head`/about to read `old->next`; B pops+frees
   `old`) that makes the Treiber `pop` a use-after-free. Which single line is the UAF, and under ASan
   what would it report?
2. Why is `push` safe to free-free (no reclamation hazard) while `pop` is not? Tie to "a new node is
   visible to one thread until linked" `[CIA §7.2.2]`.
3. The threads-in-pop scheme (§4) can leak under sustained load. Construct the workload where the
   pending list grows forever, and explain why (when is the counter ever 0?).
4. `atomic<shared_ptr<Node>>` makes reclamation trivially correct. Why might it still be the *wrong*
   choice for a high-throughput stack? (Lesson 12.4 — what does every node touch cost now?)

## My summary
