// Lesson 8 — reproduce the measured new/delete facts on YOUR machine (alignment, allocate-before-
// construct order, delete order, the array cookie). Standalone program with its OWN logging
// operators (NOT the counting ones in src/). Build & run:
//   cmake -S . -B build && cmake --build build --target newdelete_probe && ./build/newdelete_probe
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>

static std::size_t last_new_size = 0;
static std::size_t last_newarr_size = 0;

void* operator new(std::size_t n) { last_new_size = n; std::printf("  [operator new(%zu)]\n", n);
    void* p = std::malloc(n); if (!p) throw std::bad_alloc{}; return p; }
void operator delete(void* p) noexcept { std::printf("  [operator delete]\n"); std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::printf("  [operator delete sized]\n"); std::free(p); }
void* operator new[](std::size_t n) { last_newarr_size = n; std::printf("  [operator new[](%zu)]\n", n);
    void* p = std::malloc(n); if (!p) throw std::bad_alloc{}; return p; }
void operator delete[](void* p) noexcept { std::printf("  [operator delete[]]\n"); std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::printf("  [operator delete[] sized]\n"); std::free(p); }

struct Noisy { Noisy() { std::puts("  Noisy() ctor"); } ~Noisy() { std::puts("  Noisy() dtor"); } int x; };
struct D { ~D() {} char c; };  // non-trivial dtor -> delete[] needs the count -> array cookie

int main() {
    std::printf("__STDCPP_DEFAULT_NEW_ALIGNMENT__ = %zu\n", (std::size_t)__STDCPP_DEFAULT_NEW_ALIGNMENT__);
    std::printf("sizeof(int)=%zu sizeof(Noisy)=%zu sizeof(D)=%zu\n", sizeof(int), sizeof(Noisy), sizeof(D));
    std::puts("new Noisy  (expect: operator new, THEN ctor):");
    Noisy* p = new Noisy;
    std::puts("delete p   (expect: dtor, THEN operator delete):");
    delete p;
    std::puts("new D[5]   (D has a destructor -> expect a cookie):");
    D* a = new D[5];
    std::printf("  5*sizeof(D)=%zu  operator new[] got=%zu  => cookie=%zu bytes\n",
                5 * sizeof(D), last_newarr_size, last_newarr_size - 5 * sizeof(D));
    delete[] a;
    return 0;
}
