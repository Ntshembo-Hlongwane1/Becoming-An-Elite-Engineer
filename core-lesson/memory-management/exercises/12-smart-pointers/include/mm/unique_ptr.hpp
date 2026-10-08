#pragma once
// Exercise 12, part 1 — UniquePtr<T>: exclusive ownership, move-only. Notes: Lesson 12.1.
//
// A raw pointer wrapped in RAII, with copy DELETED (one owner) and move that STEALS (Lesson 11.2–11.3).
// You implement the two MOVE operations; the dtor, raw ctor, accessors, deleted copies, and
// reset/release are provided. (Scalar `delete` version; custom deleters / arrays are drills.)
#include <cstddef>
#include <utility>

namespace mm {

template <class T>
class UniquePtr {
  public:
    // ---- PROVIDED ----
    UniquePtr() noexcept = default;
    explicit UniquePtr(T* p) noexcept : p_(p) {}
    ~UniquePtr() { delete p_; }                          // RAII: free on every exit (Lesson 11.1)

    UniquePtr(const UniquePtr&) = delete;                // one owner only -> no copying (12.1 §3)
    UniquePtr& operator=(const UniquePtr&) = delete;

    T* get() const noexcept { return p_; }
    T& operator*() const { return *p_; }
    T* operator->() const noexcept { return p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }

    T* release() noexcept { T* t = p_; p_ = nullptr; return t; }        // relinquish, don't delete
    void reset(T* p = nullptr) noexcept { T* old = p_; p_ = p; delete old; }  // adopt p, free old

    // ---- YOU IMPLEMENT (Lesson 12.1 §2) ----

    // Move constructor: steal o's pointer, leave o empty. MUST be noexcept (so this stub can't Todo()).
    UniquePtr(UniquePtr&& o) noexcept {
        (void)o;  // TODO: p_ = o.p_; o.p_ = nullptr;   (steal, then null the source)
    }

    // Move assignment: release what we hold, steal o's, null o out; self-move safe. noexcept.
    UniquePtr& operator=(UniquePtr&& o) noexcept {
        (void)o;  // TODO: if (this != &o) { delete p_; p_ = o.p_; o.p_ = nullptr; }
        return *this;
    }

  private:
    T* p_ = nullptr;
};

template <class T, class... Args>
UniquePtr<T> make_unique(Args&&... args) { return UniquePtr<T>(new T(std::forward<Args>(args)...)); }

}  // namespace mm
