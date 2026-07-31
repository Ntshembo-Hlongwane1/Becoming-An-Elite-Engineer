// benchmarks/support.hpp
//
// Small helpers shared by the benchmark translation units. Nothing here touches
// production code paths - it only observes them through their public API.
#pragma once

#include <chrono>
#include <iostream>
#include <streambuf>
#include <thread>

namespace bench {

// The subsystems log progress to std::cout on every state transition, and the
// Parser dumps a tree on shutdown. Left alone that output floods the benchmark
// report and adds console I/O to the measured region. Swallow it for the
// lifetime of this object, then restore the original buffer.
class SilencedCout {
public:
    SilencedCout() : original_(std::cout.rdbuf()) {
        std::cout.rdbuf(nullptr);
    }

    ~SilencedCout() {
        std::cout.rdbuf(original_);
    }

    SilencedCout(const SilencedCout&) = delete;
    SilencedCout& operator=(const SilencedCout&) = delete;

private:
    std::streambuf* original_;
};

// How often the drain detector samples the queues, and how many consecutive
// all-empty samples count as "the pipeline has finished".
//
// The quiet window must comfortably exceed the longest stall a *working*
// pipeline can exhibit, or the detector truncates the run and reports a fast,
// wrong result. The worst case is the Lexer blocking inside a single fopen()
// while all three queues happen to be empty: on a cold 7200rpm HDD that is
// tens of milliseconds, and a contended directory_iterator can stretch it
// further. With the file cache deliberately evicted every run, both the
// enumeration and every fopen() are seek-bound, so those quiet gaps are the
// normal case rather than the exception. Five seconds of continuous silence is
// far beyond any single-file stall, and is a rounding error against a build
// measured in minutes.
inline constexpr auto kPollInterval = std::chrono::milliseconds(10);
inline constexpr int kStableSamples = 500;

// Upper bound on how long we wait for the pipeline to show its first sign of
// life before giving up (e.g. an empty data/ directory).
inline constexpr auto kStartupTimeout = std::chrono::seconds(10);

// Waits until the pipeline has both started moving data AND gone quiet again.
//
// The subsystems expose no "index build complete" signal, so completion is
// inferred from the shared queues: once every queue has stayed empty across
// kStableSamples consecutive polls, no subsystem has work left in flight.
// Stopping before that point would deadlock, because Parser::OnStop() retires
// the only consumer of parserQueue while the Lexer workers may still be
// pushing into it.
template <typename PipelineT>
bool WaitForPipelineToDrain(const PipelineT& pipeline) {
    // Phase 1: wait for the first item to appear. Without this the detector
    // would see the still-empty queues of a pipeline whose threads have not
    // been scheduled yet and declare it finished immediately.
    const auto deadline = std::chrono::steady_clock::now() + kStartupTimeout;
    while (pipeline.Drained()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(kPollInterval);
    }

    // Phase 2: wait for it to go quiet and stay quiet.
    int stable = 0;
    while (stable < kStableSamples) {
        stable = pipeline.Drained() ? stable + 1 : 0;
        std::this_thread::sleep_for(kPollInterval);
    }
    return true;
}

}  // namespace bench
