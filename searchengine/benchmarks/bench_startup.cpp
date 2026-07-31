// benchmarks/bench_startup.cpp
//
// Measures the two startup paths that Kernal::StartAll() already distinguishes:
//
//   cold - Store holds no search index, so DirReader/Lexer/Parser are started
//          and the index is built from the data/ corpus ("NO INDEX" branch).
//   warm - Store already holds a search index, so the build pipeline is
//          skipped entirely ("INDEX AVAILABLE" branch).
//
// Both benchmarks drive the real subsystems through their public API. The only
// benchmark-side setup is seeding the Store in the warm case, which happens
// outside the timed region.

#include <benchmark/benchmark.h>

#include <memory>
#include <string>

#include "internal/directoryreader/directoryreader.hpp"
#include "internal/kernal/core/datastructures/ringbuffer.hpp"
#include "internal/kernal/kernal.hpp"
#include "internal/lexer/ilp.hpp"
#include "internal/lexer/lexer.hpp"
#include "internal/parser/parser.hpp"
#include "internal/store/store.hpp"

#include "coldcache.hpp"
#include "memstats.hpp"
#include "support.hpp"

namespace {

// Owns the queues and the Kernal for a single run. The subsystem state machine
// is one-shot (CREATED -> INITIALIZED -> STARTED -> STOPPED), so every
// iteration needs a freshly constructed instance.
//
// Engine is deliberately not registered: its Run loop blocks on
// std::getline(std::cin, ...), which is an interactive REPL rather than part of
// the startup path being measured.
struct Pipeline {
    RingBuffer<std::string, 1024> dirQueue;
    RingBuffer<ILP, 1024> lineQueue;
    RingBuffer<std::string, 1024> parserQueue;

    Kernal kernal;
    Store* store = nullptr;

    Pipeline() {
        kernal.Register(SubsystemId::Store, new Store());
        kernal.Register(SubsystemId::DirReader, new DirectoryReader(dirQueue));
        kernal.Register(SubsystemId::Lexer, new Lexer(dirQueue, lineQueue, parserQueue));
        kernal.Register(SubsystemId::Parser, new Parser(parserQueue));

        store = dynamic_cast<Store*>(kernal.GetSubsystem(SubsystemId::Store));
    }

    bool Drained() const {
        return dirQueue.empty() && lineQueue.empty() && parserQueue.empty();
    }
};

// Number of entries used to stand in for an already-built index in the warm
// case. Store::OnStart() only inspects whether the index is empty, so the exact
// size does not affect the measurement - it just has to be non-empty.
constexpr int kWarmIndexEntries = 1000;

void SeedIndex(Store& store, int entries) {
    for (int i = 0; i < entries; ++i) {
        store.AddSearchIndex("term" + std::to_string(i),
                             "data/doc" + std::to_string(i) + ".txt");
    }
}

// Attaches memory figures to the row Google Benchmark prints for this run.
//
// PrivateGrowth is the number to watch: it is the commit charge the run added
// on top of where the process already was, so it isolates what building the
// index cost. Private and PeakWS are absolute and include the ~4 MB of
// executable and runtime the process carries regardless.
//
// Caveat on PeakWS: Windows never resets a process's peak working set, so
// under --benchmark_repetitions it is the high-water mark across every
// repetition so far, not this one alone. PrivateGrowth stays per-iteration.
void ReportMemory(benchmark::State& state,
                  const bench::MemorySnapshot& before,
                  const bench::MemorySnapshot& after) {
    state.counters["PrivateGrowth_MB"] = after.privateMB - before.privateMB;
    state.counters["Private_MB"] = after.privateMB;
    state.counters["PeakWS_MB"] = after.peakWorkingSetMB;
}

// --------------------------------------------------------------------------
// cold: no index present AND nothing cached, the way a first-ever startup on a
// fresh machine behaves. The index is cold because the Store starts empty; the
// disk is cold because the OS file cache is evicted before the clock starts.
// --------------------------------------------------------------------------
void BM_Startup_Cold(benchmark::State& state) {
    bench::SilencedCout silence;

    for (auto _ : state) {
        state.PauseTiming();

        // Must happen before every iteration, not just the first: after one
        // pass the whole corpus is resident in RAM and the next run would
        // measure memory bandwidth instead of the disk.
        std::string purgeError;
        if (!bench::PurgeFileSystemCache(&purgeError)) {
            state.SkipWithError(
                ("cannot run a true cold benchmark: " + purgeError +
                 ". Re-run run.sh from an Administrator shell.").c_str());
            state.ResumeTiming();
            break;
        }

        auto pipeline = std::make_unique<Pipeline>();
        const auto before = bench::TakeMemorySnapshot();
        state.ResumeTiming();

        pipeline->kernal.InitAll();
        pipeline->kernal.StartAll();

        // StartAll() only spawns the threads; the index is not built until the
        // corpus has flowed all the way through to the Parser.
        const bool drained = bench::WaitForPipelineToDrain(*pipeline);

        // Sampled here, before StopAll tears the threads down, so it reflects
        // the process at its fullest: index built, queues and thread stacks
        // still alive. Reading the counters costs microseconds against a run
        // measured in minutes, so it is left inside the timed region rather
        // than distorting the boundary with a pause.
        const auto after = bench::TakeMemorySnapshot();

        pipeline->kernal.StopAll();

        state.PauseTiming();
        if (!drained) {
            state.SkipWithError("pipeline never produced work - is data/ populated?");
        }
        ReportMemory(state, before, after);
        pipeline.reset();
        state.ResumeTiming();
    }
}

// One iteration by default: the corpus is ~100k files / ~1 GB, so letting
// Google Benchmark auto-tune the iteration count would run for hours. Use
// --benchmark_repetitions=N to collect variance.
BENCHMARK(BM_Startup_Cold)
    ->Iterations(1)
    ->Unit(benchmark::kMillisecond);

// --------------------------------------------------------------------------
// warm: index already present, the build pipeline is skipped
// --------------------------------------------------------------------------
void BM_Startup_Warm(benchmark::State& state) {
    bench::SilencedCout silence;

    for (auto _ : state) {
        state.PauseTiming();
        auto pipeline = std::make_unique<Pipeline>();
        SeedIndex(*pipeline->store, kWarmIndexEntries);
        const auto before = bench::TakeMemorySnapshot();
        state.ResumeTiming();

        pipeline->kernal.InitAll();
        pipeline->kernal.StartAll();
        const auto after = bench::TakeMemorySnapshot();
        pipeline->kernal.StopAll();

        state.PauseTiming();
        ReportMemory(state, before, after);
        pipeline.reset();
        state.ResumeTiming();
    }
}

BENCHMARK(BM_Startup_Warm)
    ->Iterations(50)
    ->Unit(benchmark::kMicrosecond);

}  // namespace
