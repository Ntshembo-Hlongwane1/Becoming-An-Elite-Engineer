// Exercise 12 — SharedPtr + WeakPtr. Lesson 12.2–12.3.
#include <utility>

#include "mm/shared_ptr.hpp"
#include "minitest.hpp"

using namespace mm;

namespace {
struct Counted { static int live; int v; Counted(int x=0):v(x){++live;} ~Counted(){--live;} };
int Counted::live = 0;
template <class P> void self_copy(P& p) { p = p; }   // dodge -Wself-assign-overloaded in the test
}  // namespace

TEST(shared_use_count_tracks_owners) {
    SharedPtr<int> a(new int(42));
    CHECK_EQ(a.use_count(), 1L);
    CHECK_EQ(*a, 42);
    {
        SharedPtr<int> b = a;                 // copy -> ++strong
        CHECK_EQ(a.use_count(), 2L);
        CHECK_EQ(b.use_count(), 2L);
        CHECK(a.get() == b.get());            // same object
    }
    CHECK_EQ(a.use_count(), 1L);              // copy left scope -> --strong
}

TEST(shared_frees_object_once_at_last_owner) {
    Counted::live = 0;
    {
        SharedPtr<Counted> a = make_shared<Counted>(1);
        CHECK_EQ(Counted::live, 1);
        {
            SharedPtr<Counted> b = a, c = a;
            CHECK_EQ(a.use_count(), 3L);
            CHECK_EQ(Counted::live, 1);        // still ONE object, shared
        }
        CHECK_EQ(a.use_count(), 1L);
        CHECK_EQ(Counted::live, 1);
    }
    CHECK_EQ(Counted::live, 0);                // destroyed exactly once by the last owner (ASan too)
}

TEST(shared_move_transfers_without_touching_count) {
    SharedPtr<int> a(new int(5));
    SharedPtr<int> b = a;
    CHECK_EQ(a.use_count(), 2L);
    SharedPtr<int> c = std::move(a);           // move: no count change
    CHECK_EQ(c.use_count(), 2L);
    CHECK(a.get() == nullptr);                  // source emptied
    CHECK_EQ(*c, 5);
}

TEST(shared_copy_assign_is_self_safe) {
    Counted::live = 0;
    {
        SharedPtr<Counted> a = make_shared<Counted>(1);
        SharedPtr<Counted> b = make_shared<Counted>(2);
        b = a;                                  // b's object released; now shares a's
        CHECK_EQ(a.use_count(), 2L);
        CHECK_EQ(b->v, 1);
        self_copy(a);                           // self copy-assign must be a no-op, not a free
        CHECK_EQ(a.use_count(), 2L);
        CHECK_EQ(a->v, 1);
    }
    CHECK_EQ(Counted::live, 0);
}

TEST(weak_does_not_keep_object_alive) {
    Counted::live = 0;
    WeakPtr<Counted> w;
    {
        SharedPtr<Counted> s = make_shared<Counted>(7);
        w = WeakPtr<Counted>(s);
        CHECK_EQ(s.use_count(), 1L);            // weak does NOT raise the strong count
        CHECK(!w.expired());
        SharedPtr<Counted> locked = w.lock();   // alive -> non-empty, strong bumps
        CHECK(locked);
        CHECK_EQ(locked->v, 7);
        CHECK_EQ(s.use_count(), 2L);
    }
    CHECK_EQ(Counted::live, 0);                 // object gone once the shared_ptr left
    CHECK(w.expired());                          // weak now reports expired
    CHECK(!w.lock());                            // and lock() returns empty (no UAF)
}

TEST(weak_lock_keeps_alive_while_held) {
    Counted::live = 0;
    WeakPtr<Counted> w;
    SharedPtr<Counted> keep;
    {
        SharedPtr<Counted> s = make_shared<Counted>(1);
        w = WeakPtr<Counted>(s);
        keep = w.lock();                         // promote weak -> strong; keeps object alive
    }                                            // s gone, but `keep` still owns it
    CHECK(!w.expired());
    CHECK_EQ(Counted::live, 1);
    CHECK_EQ(keep->v, 1);
    keep = SharedPtr<Counted>();                 // drop the last owner
    CHECK(w.expired());
    CHECK_EQ(Counted::live, 0);
}

// Breaking a reference cycle with weak_ptr (Lesson 12.3). All-shared leaks; one weak edge frees.
struct Node { static int live; SharedPtr<Node> next; WeakPtr<Node> weak_back; Node(){++live;} ~Node(){--live;} };
int Node::live = 0;

TEST(all_shared_cycle_raises_counts_and_would_leak) {
    Node::live = 0;
    {
        SharedPtr<Node> a = make_shared<Node>();
        SharedPtr<Node> b = make_shared<Node>();
        a->next = b;
        b->next = a;                             // shared<->shared cycle
        CHECK_EQ(a.use_count(), 2L);             // each node: external owner + the other node
        CHECK_EQ(b.use_count(), 2L);
        // If a and b left scope now, each strong would drop 2->1 (held by the other) and never reach
        // 0 — the leak the notes measured. We prove the mechanism via the counts, then break the cycle
        // so this test stays leak-clean under ASan:
        a->next = SharedPtr<Node>();
        b->next = SharedPtr<Node>();
        CHECK_EQ(a.use_count(), 1L);             // cycle broken -> only external owners remain
    }
    CHECK_EQ(Node::live, 0);                      // now both free normally
}

TEST(weak_edge_breaks_the_cycle) {
    Node::live = 0;
    {
        SharedPtr<Node> a = make_shared<Node>();
        SharedPtr<Node> b = make_shared<Node>();
        a->next = b;
        b->weak_back = WeakPtr<Node>(a);         // one edge weak -> no cycle
    }
    CHECK_EQ(Node::live, 0);                      // both freed
}
