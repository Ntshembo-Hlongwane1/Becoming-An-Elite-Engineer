// PROVIDED — do not edit. The rest of the operator new / operator delete family (Lesson 8.4 §1),
// all delegating to the two CORE operators you implement in counting_new.cpp. Keeping the whole
// family matched and routed through one place is what prevents the "freed by the wrong operator"
// mismatch of Lesson 8.5.
#include <cstddef>
#include <new>

void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }

// Sized array delete: the compiler prefers sized deletes when available (Lesson 8.1 (measured),
// 8.4 §3). We ignore the size and trust our own header. (The sized NON-array delete lives next to
// the core operator delete in counting_new.cpp, so each TU defines a matched sized/unsized pair.)
void operator delete[](void* p, std::size_t) noexcept { ::operator delete[](p); }

// Nothrow forms (Lesson 8.2 §2): turn a bad_alloc from the core into a nullptr return.
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try { return ::operator new(n); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    try { return ::operator new[](n); } catch (...) { return nullptr; }
}
void operator delete(void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { ::operator delete[](p); }
