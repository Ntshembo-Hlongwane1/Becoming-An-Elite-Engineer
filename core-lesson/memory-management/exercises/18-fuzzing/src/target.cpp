#include "mm/fuzz.hpp"

namespace {
// Code under test: a tiny "parser" with a magic gate guarding a heap-buffer-overflow. Each magic byte
// is a separate branch so coverage feedback can crack them one at a time (Lesson 18.2 §1 / 18.1 §4).
void parse(const std::uint8_t* d, std::size_t n) {
    if (n < 5) return;
    if (d[0] != 'F') return;
    if (d[1] != 'U') return;
    if (d[2] != 'Z') return;
    if (d[3] != 'Z') return;
    std::uint8_t len = d[4];
    std::uint8_t* buf = new std::uint8_t[16];
    for (std::uint8_t i = 0; i < len; ++i) buf[i] = i;   // len > 16 -> heap-buffer-overflow (ASan)
    delete[] buf;
}
}  // namespace

namespace mm {
// YOU IMPLEMENT (Lesson 18.3 §1): the harness. Map the fuzzer's bytes to the code under test.
// Here it's a straight parser, so just hand the bytes to parse(). (The drills harness your Lesson-7
// allocator / Lesson-11/13 containers with a bytes-as-bytecode mapping instead.)
void fuzz_one(const std::uint8_t* data, std::size_t size) {
    (void)data; (void)size;
    // TODO(Lesson 18.3 §1): parse(data, size);
    // STUB: does nothing -> target code never runs -> no coverage, no crash. Implement me.
}
}  // namespace mm
