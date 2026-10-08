#include "mm/fuzz.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sanitizer/common_interface_defs.h>   // __sanitizer_set_death_callback

namespace mm {

std::uint8_t g_covmap[kMapSize];
static std::uint8_t g_total[kMapSize];          // cumulative: every edge ever seen

// ===== YOU IMPLEMENT (Lesson 18.2 §3–§4) =====

// Return true iff g_covmap contains an edge not already in g_total; fold the new edges into g_total.
// This is the heart of coverage-guided fuzzing: inputs that reach NEW code get kept (Lesson 18.2 §3).
// STUB: always false -> the fuzzer is coverage-BLIND (pure random) and will NOT crack the magic gate
// in the budget (Lesson 18.1 §4). Implement it and it finds the bug in < 1 s.
bool interesting() {
    // TODO: for i in [0,kMapSize): if (g_covmap[i] && !g_total[i]) { g_total[i]=1; novel=true; } return novel;
    return false;
}

// Perturb `input` in place (bit flips / random bytes / grow). Advance rng_state yourself (a tiny PRNG)
// so runs are reproducible. Keep it cheap — it runs millions of times (Lesson 18.2 §4).
void mutate(std::vector<std::uint8_t>& input, std::uint64_t& rng_state) {
    (void)input; (void)rng_state;
    // TODO(Lesson 18.2 §4): xorshift rng_state; do a few ops, e.g.:
    //   v[r % v.size()] ^= 1 << (r % 8);   v[r % v.size()] = (uint8_t)r;   v.push_back((uint8_t)r);
    // STUB: does nothing -> every input stays the seed -> no bug found. Implement me.
}

// ===== PROVIDED: the coverage callback (NOT coverage-instrumented — this TU has no -fsanitize-coverage),
// the crash-reproducer death callback, and the fuzzing loop. =====

extern "C" void __sanitizer_cov_trace_pc(void) {   // called at each instrumented edge of the TARGET
    std::uintptr_t pc = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    g_covmap[(pc >> 4) & (kMapSize - 1)] = 1;
}

static const std::uint8_t* g_cur = nullptr; static std::size_t g_cur_n = 0; static long g_iter = 0;
static void on_death() {                           // on sanitizer abort: save the reproducer
    if (g_cur) { FILE* f = std::fopen("crash-input", "wb"); if (f) { std::fwrite(g_cur, 1, g_cur_n, f); std::fclose(f); } }
    std::fprintf(stderr, "[fuzzer] crash at iter %ld; %zu-byte reproducer saved to crash-input\n", g_iter, g_cur_n);
}

int main(int argc, char** argv) {
    long budget = argc > 1 ? std::atol(argv[1]) : 5000000;
    __sanitizer_set_death_callback(on_death);
    std::uint64_t rng = 0x9E3779B97F4A7C15ull;
    std::vector<std::vector<std::uint8_t>> corpus;
    corpus.push_back({'x'});                        // trivial seed (Lesson 18.3 §4)
    for (g_iter = 0; g_iter < budget; ++g_iter) {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;          // pick a corpus entry
        std::vector<std::uint8_t> in = corpus[rng % corpus.size()];
        mutate(in, rng);                                               // your mutator
        g_cur = in.data(); g_cur_n = in.size();
        std::memset(g_covmap, 0, kMapSize);                            // per-run coverage
        fuzz_one(in.data(), in.size());                                // run target (may crash -> on_death)
        if (interesting()) corpus.push_back(in);                       // your feedback: keep new-coverage inputs
    }
    std::printf("no crash in %ld iters; corpus=%zu\n", budget, corpus.size());
    return 0;
}
}  // namespace mm

int main(int argc, char** argv) { return mm::main(argc, argv); }
