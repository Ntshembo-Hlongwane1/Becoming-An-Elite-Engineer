#pragma once
// Exercise 11 — a Vector<T> with the strong exception guarantee. Notes: Lesson 11.
//
// This is a growable, owning dynamic array built directly on Lesson 8 (operator new for raw storage +
// placement new to construct elements) and Lesson 9 (alignment). Because it owns a raw allocation it
// must implement the RULE OF FIVE correctly, and its growth (reserve/emplace_back) must preserve the
// STRONG exception guarantee via std::move_if_noexcept (Lesson 11.3–11.4).
//
// Being a template, everything lives in this header. The sections marked "YOU IMPLEMENT" are yours;
// the "PROVIDED" sections (raw storage, element access, clear, swap, destructor, push_back) are done
// — read them, they tell you the invariants your code must preserve:
//   * data_ points to raw storage for cap_ elements (or nullptr if cap_==0);
//   * elements [0, size_) are CONSTRUCTED; [size_, cap_) are raw;
//   * the destructor destroys [0,size_) then frees the storage.

#include <cstddef>
#include <new>
#include <stdexcept>
#include <utility>

#include "mm/todo.hpp"

namespace mm {

template <class T>
class Vector {
  public:
    // ---- PROVIDED: construction / destruction ----
    Vector() noexcept = default;

    explicit Vector(std::size_t n) {           // n default-constructed elements
        reserve(n);
        for (std::size_t i = 0; i < n; ++i) { ::new (data_ + i) T(); ++size_; }
    }

    ~Vector() {                                // rule of five: the destructor (noexcept)
        clear();                               // destroy constructed elements
        free_raw(data_);                       // free the raw storage
    }

    // =====================================================================================
    // YOU IMPLEMENT — the rule of five (copy/move) + growth (reserve/emplace_back).
    // Reference behaviour is specified in each comment; see Lesson 11.2–11.4.
    // =====================================================================================

    // Copy constructor — DEEP copy (Lesson 11.2): own a SEPARATE buffer of o.size_ elements and
    // copy-construct each from o. If an element copy throws, destroy the ones you built, free the
    // buffer, and rethrow (nothing leaks — this object isn't fully constructed). Use alloc_raw +
    // placement new; grow `size_` as you construct so clear() destroys the right count on failure.
    Vector(const Vector& o) {
        (void)o;
        Todo("Vector copy constructor");       // leave members empty (NSDMIs) so a throw here is clean
    }

    // Move constructor — STEAL o's buffer (data_/size_/cap_) and leave o EMPTY & valid (Lesson 11.3).
    // MUST be noexcept (so containers move, not copy). NOTE: a Todo() here would call std::terminate
    // (throwing from noexcept), so this stub just leaves *this empty — fix it to actually steal.
    Vector(Vector&& o) noexcept {
        (void)o;  // TODO(Lesson 11.3): steal o's data_/size_/cap_, then null o out.
    }

    // Copy assignment — use COPY-AND-SWAP (Lesson 11.4 §2): construct a temporary copy of o (all the
    // work that can throw), then swap(tmp) (noexcept) to commit. Strong guarantee + self-assign safe.
    Vector& operator=(const Vector& o) {
        (void)o;
        Todo("Vector copy assignment");
    }

    // Move assignment — release what we hold, steal o's buffer, null o out; guard against self-move
    // (Lesson 11.3). noexcept, so no Todo() here — implement it; the stub below does nothing.
    Vector& operator=(Vector&& o) noexcept {
        (void)o;  // TODO(Lesson 11.3): clear()+free_raw(data_), steal o, null o out (if this != &o).
        return *this;
    }

    // reserve — ensure capacity >= new_cap. If growth is needed: alloc_raw a new buffer, relocate the
    // existing elements with std::move_if_noexcept (move if T's move is noexcept, else COPY — Lesson
    // 11.4 §3), then destroy the old elements, free the old buffer, and adopt the new one. STRONG
    // guarantee: if a relocation throws, destroy the new elements built so far, free the new buffer,
    // and rethrow with *this UNCHANGED.
    void reserve(std::size_t new_cap) {
        (void)new_cap;
        Todo("Vector::reserve");
    }

    // emplace_back — grow if full (reserve(next_capacity())), then placement-new a T from args at the
    // end, bump size_ only on success, and return a reference to it (Lesson 11.3/11.4).
    template <class... Args>
    T& emplace_back(Args&&... args) {
        (void)sizeof...(args);
        Todo("Vector::emplace_back");
    }

    // =====================================================================================
    // PROVIDED — element access, capacity, and the small helpers your code above relies on.
    // =====================================================================================

    void push_back(const T& v) { emplace_back(v); }       // delegate to emplace_back
    void push_back(T&& v) { emplace_back(std::move(v)); }

    T& operator[](std::size_t i) { return data_[i]; }
    const T& operator[](std::size_t i) const { return data_[i]; }
    T& at(std::size_t i) { if (i >= size_) throw std::out_of_range("Vector::at"); return data_[i]; }
    const T& at(std::size_t i) const { if (i >= size_) throw std::out_of_range("Vector::at"); return data_[i]; }

    T* data() noexcept { return data_; }
    const T* data() const noexcept { return data_; }
    T* begin() noexcept { return data_; }
    T* end() noexcept { return data_ + size_; }
    const T* begin() const noexcept { return data_; }
    const T* end() const noexcept { return data_ + size_; }

    std::size_t size() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return cap_; }
    bool empty() const noexcept { return size_ == 0; }

    void clear() noexcept {                                // destroy all elements; keep capacity
        for (std::size_t i = 0; i < size_; ++i) data_[i].~T();
        size_ = 0;
    }

    void swap(Vector& o) noexcept {                        // noexcept: only exchanges the 3 members
        std::swap(data_, o.data_);
        std::swap(size_, o.size_);
        std::swap(cap_, o.cap_);
    }

  private:
    // Raw storage for n elements of T, correctly aligned for T (Lesson 8.2 §3 / Lesson 9). No objects
    // are constructed here — placement new does that. free_raw(nullptr) is a safe no-op.
    static T* alloc_raw(std::size_t n) {
        if (n == 0) return nullptr;
        return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t(alignof(T))));
    }
    static void free_raw(T* p) noexcept {
        ::operator delete(p, std::align_val_t(alignof(T)));
    }
    std::size_t next_capacity() const { return cap_ ? cap_ * 2 : 1; }   // doubling growth

    T* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t cap_ = 0;
};

template <class T>
void swap(Vector<T>& a, Vector<T>& b) noexcept { a.swap(b); }

}  // namespace mm
