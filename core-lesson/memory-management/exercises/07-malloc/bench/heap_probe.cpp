// Lesson 7.1 — reproduce the measured heap facts on YOUR machine with the REAL malloc.
// Build & run:  cmake -S . -B build && cmake --build build --target heap_probe && ./build/heap_probe
// (Uses glibc malloc directly; it does NOT use your FreeListAllocator.)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>   // malloc_usable_size
#include <unistd.h>   // sbrk

int main() {
    std::printf("alignof(max_align_t) = %zu\n", alignof(max_align_t));

    void* b0 = sbrk(0);
    void* a = std::malloc(100);
    void* b1 = sbrk(0);
    std::printf("malloc(100): ptr=%p  break moved %ld B  usable=%zu  ptr%%16=%zu\n",
                a, (long)((char*)b1 - (char*)b0), malloc_usable_size(a),
                (std::uintptr_t)a % 16);

    void* x = std::malloc(24);
    void* y = std::malloc(24);
    std::printf("malloc(24) x,y stride = %ld B (the chunk size)  usable(x)=%zu\n",
                (long)((char*)y - (char*)x), malloc_usable_size(x));

    void* s = sbrk(0);
    void* big = std::malloc(200000);          // >= 128 kB MMAP_THRESHOLD => mmap
    void* e = sbrk(0);
    std::printf("malloc(200000): break moved %ld B (0 => served by mmap); far from heap=%s\n",
                (long)((char*)e - (char*)s),
                ((char*)big - (char*)x) > (1L << 40) ? "yes" : "no");

    void* g0 = sbrk(0);
    for (int i = 0; i < 65536; ++i) { void* q = std::malloc(64); (void)q; }
    void* g1 = sbrk(0);
    std::printf("break grew %ld B for ~4MB of 64-B requests (sbrk moved the heap up)\n",
                (long)((char*)g1 - (char*)g0));

    std::free(a); std::free(x); std::free(y); std::free(big);
    void* z = std::malloc(0);
    std::printf("malloc(0)=%p (unique, freeable); free(NULL) is a no-op\n", z);
    std::free(z); std::free(nullptr);
    return 0;
}
