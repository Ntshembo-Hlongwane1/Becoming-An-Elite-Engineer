#pragma once
// Exercise 13 — SmallVector<T,N>: a vector with N elements of INLINE storage that spills to the heap.
// Notes: Lesson 13.4. Built on Lesson 11 (rule of five, move_if_noexcept, strong guarantee) + Lesson
// 8/9 (placement new, alignment).
//
// Layout (Lesson 13.4 §1): an in-object aligned buffer for N elements, plus data_ that points either
// at that inline buffer or at a heap buffer. is_inline() == "data_ points at my own inline buffer".
// A fresh SmallVector is inline with capacity N and owns no heap.
//
// You implement the growth + rule of five: reserve, emplace_back, copy ctor, MOVE ctor, copy assign,
// MOVE assign. The move operations are the point of the exercise — when the source is INLINE you must
// MOVE its elements into your own inline buffer (you can't steal a pointer into the source object);
// when the source is on the HEAP you steal the buffer pointer (Lesson 13.4 §3). The dtor, raw
// alloc/free, accessors, clear, and push_back are provided.
#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <utility>

#include "mm/todo.hpp"

namespace mm {

template <class T, std::size_t N>
class SmallVector {
    static_assert(N >= 1, "inline capacity N must be at least 1");

  public:
    // ---- PROVIDED: construction / destruction ----
    SmallVector() noexcept : data_(inline_ptr()), size_(0), cap_(N) {}

    ~SmallVector() {
        clear();
        if (!is_inline()) free_raw(data_);          // free the HEAP buffer only, never the inline one
    }

    // =====================================================================================
    // YOU IMPLEMENT — Lesson 13.4. (All may throw -> none are noexcept, so you can see where.)
    // =====================================================================================

    // Grow capacity to >= new_cap. Allocate a heap buffer, relocate with std::move_if_noexcept
    // (strong guarantee, Lesson 11.4 §3), destroy old elements, free the old buffer ONLY if it was
    // heap (if (!is_inline())), then adopt the new buffer. No-op if new_cap <= cap_.
    void reserve(std::size_t new_cap) {
        // TODO(Lesson 13.4 §2): if new_cap<=cap_ return; else alloc_raw(new_cap), relocate with
        // std::move_if_noexcept (destroy the new ones + free_raw(nd) + rethrow on throw), destroy old
        // elements, free_raw(data_) ONLY if (!is_inline()), then data_=nd; cap_=new_cap.
        (void)new_cap;
        Todo("SmallVector::reserve");
    }

    // Construct a new element at the end from args, growing (spilling) if full. Returns a reference.
    template <class... Args>
    T& emplace_back(Args&&... args) {
        // TODO(Lesson 13.4 §2): if full (size_==cap_) reserve(cap_*2); placement-new T(args...) at
        // data_+size_; ++size_; return data_[size_-1].
        (void)sizeof...(args);
        Todo("SmallVector::emplace_back");
    }

    // Deep copy: start inline, reserve if o is bigger than N, copy-construct each element.
    SmallVector(const SmallVector& o) : data_(inline_ptr()), size_(0), cap_(N) {
        (void)o;
        // TODO(Lesson 13.4 §3 / Lesson 11.2): deep copy — reserve(o.size_) then copy-construct each
        // element; on throw, clear() + free_raw if heap + rethrow.
        Todo("SmallVector copy constructor");
    }

    // Move: INLINE source -> move its elements into OUR inline buffer; HEAP source -> steal the buffer
    // pointer and reset the source to empty-inline. (Lesson 13.4 §3 — the heart of the exercise.)
    SmallVector(SmallVector&& o) : data_(inline_ptr()), size_(0), cap_(N) {
        (void)o;
        // TODO(Lesson 13.4 §3 — THE point of the exercise):
        //   if (o.is_inline())  MOVE o's elements into OUR inline buffer, then o.clear();
        //   else                steal o.data_/cap_/size_, then reset o to empty-inline.
        Todo("SmallVector move constructor");
    }

    // Copy assignment: copy-and-move (copy o into a temporary — strong guarantee — then move-assign).
    SmallVector& operator=(const SmallVector& o) {
        (void)o;
        // TODO(Lesson 13.4): copy-and-move -> SmallVector tmp(o); *this = std::move(tmp);
        Todo("SmallVector copy assignment");
    }

    // Move assignment: release what we hold, then relocate-or-steal from o (same split as the move
    // ctor). Self-move safe.
    SmallVector& operator=(SmallVector&& o) {
        (void)o;
        // TODO(Lesson 13.4 §3): self-move guard; clear(); release heap + back to inline if (!is_inline());
        // then the same inline-relocate / heap-steal split as the move constructor.
        Todo("SmallVector move assignment");
    }

    // =====================================================================================
    // PROVIDED — access, capacity, and the helpers your code above relies on.
    // =====================================================================================
    void push_back(const T& v) { emplace_back(v); }
    void push_back(T&& v) { emplace_back(std::move(v)); }

    T& operator[](std::size_t i) { return data_[i]; }
    const T& operator[](std::size_t i) const { return data_[i]; }
    T& at(std::size_t i) { if (i >= size_) throw std::out_of_range("SmallVector::at"); return data_[i]; }

    T* data() noexcept { return data_; }
    const T* data() const noexcept { return data_; }
    T* begin() noexcept { return data_; }
    T* end() noexcept { return data_ + size_; }

    std::size_t size() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return cap_; }
    bool empty() const noexcept { return size_ == 0; }
    bool is_inline() const noexcept { return data_ == reinterpret_cast<const T*>(inline_); }

    void clear() noexcept {
        for (std::size_t i = 0; i < size_; ++i) data_[i].~T();
        size_ = 0;
    }

  private:
    T* inline_ptr() noexcept { return reinterpret_cast<T*>(inline_); }
    static T* alloc_raw(std::size_t n) {
        return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t(alignof(T))));
    }
    static void free_raw(T* p) noexcept { ::operator delete(p, std::align_val_t(alignof(T))); }

    alignas(T) std::byte inline_[N * sizeof(T)];   // in-object storage for N elements (Lesson 13.2)
    T* data_;
    std::size_t size_;
    std::size_t cap_;
};

}  // namespace mm
