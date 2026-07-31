// benchmarks/memstats.hpp
//
// Reads the process memory counters so a benchmark can report how much memory
// a run consumed alongside how long it took.
//
// Two different numbers matter here and they answer different questions:
//
//   Private bytes (commit charge) - how much memory the process actually asked
//   the OS for. Unaffected by the working-set trimming that the cold-cache
//   purge performs, so this is the honest "how much did we allocate" figure.
//
//   Peak working set - the high-water mark of physical RAM actually resident.
//   Useful for spotting a run that ballooned mid-build, since it records the
//   maximum even though it is read after the fact.
#pragma once

#ifdef _WIN32

#include <windows.h>
// psapi.h must follow windows.h.
#include <psapi.h>

namespace bench {

struct MemorySnapshot {
    double workingSetMB = 0.0;
    double peakWorkingSetMB = 0.0;
    double privateMB = 0.0;
};

inline MemorySnapshot TakeMemorySnapshot() {
    MemorySnapshot snapshot;

    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);

    // The EX struct is a superset of PROCESS_MEMORY_COUNTERS; the API takes the
    // base type and uses cb to work out which one it was handed.
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters))) {
        constexpr double kBytesPerMB = 1024.0 * 1024.0;
        snapshot.workingSetMB = static_cast<double>(counters.WorkingSetSize) / kBytesPerMB;
        snapshot.peakWorkingSetMB = static_cast<double>(counters.PeakWorkingSetSize) / kBytesPerMB;
        snapshot.privateMB = static_cast<double>(counters.PrivateUsage) / kBytesPerMB;
    }

    return snapshot;
}

}  // namespace bench

#else

namespace bench {

struct MemorySnapshot {
    double workingSetMB = 0.0;
    double peakWorkingSetMB = 0.0;
    double privateMB = 0.0;
};

inline MemorySnapshot TakeMemorySnapshot() { return MemorySnapshot{}; }

}  // namespace bench

#endif  // _WIN32
