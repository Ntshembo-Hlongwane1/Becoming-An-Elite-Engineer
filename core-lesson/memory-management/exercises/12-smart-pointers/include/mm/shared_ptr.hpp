#pragma once
// Exercise 12, parts 2 & 3 — SharedPtr<T> + WeakPtr<T> with a reference-counted control block.
// Notes: Lesson 12.2–12.3. SharedPtr and WeakPtr are mutually dependent (lock() returns a SharedPtr),
// so they live together here.
//
// The control block (detail::CbBase/Cb) and its ATOMIC count operations are PROVIDED — those are the
// thread-safety-critical parts (Lesson 12.4). You implement the ownership PROTOCOL in the smart
// pointers: ++strong on copy, steal on move, release on overwrite (SharedPtr); ++weak and lock()
// (WeakPtr). Read the provided CbBase first — its methods are the vocabulary your code uses.
#include <atomic>
#include <cstddef>
#include <utility>

namespace mm {

namespace detail {

// One control block is shared by all SharedPtr/WeakPtr copies of an object (Lesson 12.2 §2).
struct CbBase {
    std::atomic<long> strong{1};   // # of SharedPtr owners (object dies at 0)
    // weak = (# of WeakPtr observers) + 1 while any strong owner exists. The "+1" is a self weak-ref
    // held collectively by the strong owners (released in dec_strong). It makes the control block
    // outlive the object safely even when the object's own destructor drops a WeakPtr to this block.
    std::atomic<long> weak{1};

    virtual void destroy_object() noexcept = 0;   // run the object's destructor + free it
    virtual ~CbBase() = default;

    long use_count() const noexcept { return strong.load(std::memory_order_relaxed); }

    void inc_strong() noexcept { strong.fetch_add(1, std::memory_order_relaxed); }
    void dec_strong() noexcept {                                   // Lesson 12.2 §3
        if (strong.fetch_sub(1, std::memory_order_acq_rel) == 1) { // we were the last strong owner
            destroy_object();                                      // -> destroy the OBJECT, then
            dec_weak();                                            //    release the strong set's self weak-ref
        }
    }
    void inc_weak() noexcept { weak.fetch_add(1, std::memory_order_relaxed); }
    void dec_weak() noexcept {                                     // Lesson 12.3 §3
        if (weak.fetch_sub(1, std::memory_order_acq_rel) == 1)     // last weak ref (incl. the self ref)
            delete this;                                           // -> free the control block
    }
    // Increment strong ONLY if it is currently nonzero — the atomic heart of weak_ptr::lock()
    // (Lesson 12.3 §4). Returns false if the object has already been destroyed.
    bool incref_if_nonzero() noexcept {
        long s = strong.load(std::memory_order_relaxed);
        while (s != 0)
            if (strong.compare_exchange_weak(s, s + 1, std::memory_order_acq_rel,
                                             std::memory_order_relaxed))
                return true;
        return false;
    }
};

template <class T>
struct Cb : CbBase {
    T* p;
    explicit Cb(T* ptr) noexcept : p(ptr) {}
    void destroy_object() noexcept override { delete p; p = nullptr; }
};

struct adopt_t {};   // tag: construct a SharedPtr that adopts an already-counted control block

}  // namespace detail

template <class T>
class WeakPtr;

template <class T>
class SharedPtr {
  public:
    // ---- PROVIDED ----
    SharedPtr() noexcept = default;
    explicit SharedPtr(T* p) : p_(p), cb_(p ? new detail::Cb<T>(p) : nullptr) {}  // strong=1
    ~SharedPtr() { if (cb_) cb_->dec_strong(); }

    T* get() const noexcept { return p_; }
    T& operator*() const { return *p_; }
    T* operator->() const noexcept { return p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }
    long use_count() const noexcept { return cb_ ? cb_->use_count() : 0; }

    void swap(SharedPtr& o) noexcept { std::swap(p_, o.p_); std::swap(cb_, o.cb_); }

    // adopt an already-incremented control block (used by WeakPtr::lock(); PROVIDED, do not change)
    SharedPtr(detail::adopt_t, T* p, detail::CbBase* cb) noexcept : p_(p), cb_(cb) {}

    // ---- YOU IMPLEMENT (Lesson 12.2 §3) ----

    // Copy: share o's object + control block, and ++strong.
    SharedPtr(const SharedPtr& o) noexcept {
        (void)o;  // TODO: p_ = o.p_; cb_ = o.cb_; if (cb_) cb_->inc_strong();   (share + ++strong)
    }

    // Move: steal o's pointers, no count change, leave o empty. noexcept.
    SharedPtr(SharedPtr&& o) noexcept {
        (void)o;  // TODO: steal o.p_/o.cb_ (no count change), then null o out.
    }

    // Copy assignment: copy-and-swap (Lesson 11.4 §2) — strong guarantee + self-assignment safe.
    SharedPtr& operator=(const SharedPtr& o) noexcept {
        (void)o;  // TODO: copy-and-swap -> SharedPtr tmp(o); swap(tmp); (strong guarantee + self-safe)
        return *this;
    }

    // Move assignment: release what we hold, steal o's, null o out; self-move safe. noexcept.
    SharedPtr& operator=(SharedPtr&& o) noexcept {
        (void)o;  // TODO: if (this!=&o) { if(cb_) cb_->dec_strong(); steal o; null o out; }
        return *this;
    }

  private:
    template <class U> friend class WeakPtr;
    T* p_ = nullptr;
    detail::CbBase* cb_ = nullptr;
};

template <class T>
class WeakPtr {
  public:
    // ---- PROVIDED ----
    WeakPtr() noexcept = default;
    ~WeakPtr() { if (cb_) cb_->dec_weak(); }

    WeakPtr(const WeakPtr& o) noexcept : p_(o.p_), cb_(o.cb_) { if (cb_) cb_->inc_weak(); }
    WeakPtr(WeakPtr&& o) noexcept : p_(o.p_), cb_(o.cb_) { o.p_ = nullptr; o.cb_ = nullptr; }
    WeakPtr& operator=(const WeakPtr& o) noexcept { WeakPtr tmp(o); swap(tmp); return *this; }
    WeakPtr& operator=(WeakPtr&& o) noexcept {
        if (this != &o) { if (cb_) cb_->dec_weak(); p_ = o.p_; cb_ = o.cb_; o.p_ = nullptr; o.cb_ = nullptr; }
        return *this;
    }
    void swap(WeakPtr& o) noexcept { std::swap(p_, o.p_); std::swap(cb_, o.cb_); }

    // ---- YOU IMPLEMENT (Lesson 12.3) ----

    // Construct from a SharedPtr: observe its object + control block, and ++weak (NOT strong).
    WeakPtr(const SharedPtr<T>& s) noexcept {
        (void)s;  // TODO: observe s.p_/s.cb_ and ++weak (NOT strong). (SharedPtr befriends WeakPtr.)
    }

    // expired(): true once the object has been destroyed (strong count == 0). noexcept.
    bool expired() const noexcept { return true; /* TODO: !cb_ || cb_->use_count() == 0 */ }

    // lock(): atomically get a SharedPtr if the object is still alive, else an empty SharedPtr.
    // Use cb_->incref_if_nonzero(); on success, adopt the (already-incremented) control block.
    SharedPtr<T> lock() const noexcept {
        // TODO: if (cb_ && cb_->incref_if_nonzero()) return SharedPtr<T>(detail::adopt_t{}, p_, cb_);
        return SharedPtr<T>();   // stub: always empty
    }

  private:
    T* p_ = nullptr;
    detail::CbBase* cb_ = nullptr;
};

template <class T, class... Args>
SharedPtr<T> make_shared(Args&&... args) { return SharedPtr<T>(new T(std::forward<Args>(args)...)); }

}  // namespace mm
