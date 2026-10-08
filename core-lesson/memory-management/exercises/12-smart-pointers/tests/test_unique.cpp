// Exercise 12 — UniquePtr. Lesson 12.1.
#include <type_traits>
#include <utility>

#include "mm/unique_ptr.hpp"
#include "minitest.hpp"

using namespace mm;

namespace {
struct Counted { static int live; int v; Counted(int x=0):v(x){++live;} ~Counted(){--live;} };
int Counted::live = 0;
template <class P> void self_move(P& p) { p = std::move(p); }   // dodge -Wself-move in the test
}  // namespace

TEST(unique_is_move_only) {
    static_assert(!std::is_copy_constructible_v<UniquePtr<int>>, "must not be copyable");
    static_assert(!std::is_copy_assignable_v<UniquePtr<int>>, "must not be copy-assignable");
    static_assert(std::is_nothrow_move_constructible_v<UniquePtr<int>>, "move ctor must be noexcept");
    static_assert(std::is_nothrow_move_assignable_v<UniquePtr<int>>, "move assign must be noexcept");
    CHECK(true);
}

TEST(unique_owns_and_derefs) {
    UniquePtr<int> p(new int(42));
    CHECK(p);
    CHECK_EQ(*p, 42);
    *p = 7;
    CHECK_EQ(*p, 7);
    CHECK(p.get() != nullptr);
}

TEST(unique_move_ctor_transfers_ownership) {
    UniquePtr<int> a(new int(5));
    int* raw = a.get();
    UniquePtr<int> b = std::move(a);
    CHECK(b.get() == raw);           // b now owns it
    CHECK(a.get() == nullptr);        // a emptied
    CHECK_EQ(*b, 5);
}

TEST(unique_move_assign_releases_old_and_steals) {
    UniquePtr<int> a(new int(1)), b(new int(2));
    int* raw = b.get();
    a = std::move(b);                 // a's old int freed; a steals b's
    CHECK(a.get() == raw);
    CHECK(b.get() == nullptr);
    CHECK_EQ(*a, 2);
    self_move(a);                     // self-move must not corrupt/free-then-use
    CHECK(a.get() == raw);
}

TEST(unique_release_relinquishes_without_deleting) {
    Counted::live = 0;
    int* raw = nullptr;
    {
        UniquePtr<Counted> p(new Counted(9));
        CHECK_EQ(Counted::live, 1);
        raw = reinterpret_cast<int*>(p.release());
        CHECK(p.get() == nullptr);
        CHECK_EQ(Counted::live, 1);   // released, NOT deleted
    }
    CHECK_EQ(Counted::live, 1);        // still alive after the unique_ptr died
    delete reinterpret_cast<Counted*>(raw);
    CHECK_EQ(Counted::live, 0);
}

TEST(unique_reset_frees_old) {
    Counted::live = 0;
    UniquePtr<Counted> p(new Counted(1));
    CHECK_EQ(Counted::live, 1);
    p.reset(new Counted(2));           // old freed, new adopted
    CHECK_EQ(Counted::live, 1);
    CHECK_EQ(p->v, 2);
    p.reset();                         // frees, becomes empty
    CHECK_EQ(Counted::live, 0);
    CHECK(!p);
}

TEST(unique_destructor_frees_exactly_once) {
    Counted::live = 0;
    { UniquePtr<Counted> p = make_unique<Counted>(3); CHECK_EQ(Counted::live, 1); }
    CHECK_EQ(Counted::live, 0);        // RAII freed it (ASan also confirms no leak / no double-free)
}
