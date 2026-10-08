#!/usr/bin/env bash
# Build the coverage-guided fuzzer and run it; a seeded heap-overflow should be found and reproduced.
#   ./run.sh [budget]      default budget 20000000 iterations, 45s wall cap
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; cd "$ROOT"
BUDGET="${1:-20000000}"; mkdir -p build; rm -f crash-input
echo "── building (engine: no coverage; target: -O0 + trace-pc coverage) ──"
g++ -std=c++20 -O2 -g -fsanitize=address -Iinclude -c src/engine.cpp -o build/engine.o || exit 2
g++ -std=c++20 -O0 -g -fsanitize=address -fsanitize-coverage=trace-pc -Iinclude -c src/target.cpp -o build/target.cov.o || exit 2
g++ -fsanitize=address build/engine.o build/target.cov.o -o build/fuzz || exit 2
# replay target compiled WITHOUT coverage (no __sanitizer_cov_trace_pc needed)
g++ -std=c++20 -O0 -g -fsanitize=address -Iinclude src/target.cpp replay.cpp -o build/replay || exit 2
echo "── fuzzing (budget=$BUDGET, 45s cap) ──"
ASAN_OPTIONS=abort_on_error=1 timeout 45 ./build/fuzz "$BUDGET" 2>&1 | grep -E 'fuzzer\] crash|no crash|heap-buffer-overflow' | head -3
echo "──────────────────────────────────────"
if [[ -f crash-input ]]; then
    echo "reproducer found; replaying:"
    if ASAN_OPTIONS=abort_on_error=1 ./build/replay crash-input >/dev/null 2>&1; then
        echo "replay did NOT crash ❌ (non-deterministic harness?)"; exit 1
    else
        echo "BUG FOUND AND REPRODUCED ✅  (coverage-guided fuzzer cracked the gate + ASan caught the overflow)"; exit 0
    fi
else
    echo "NOT FOUND ❌  — implement interesting()/mutate()/fuzz_one() (coverage feedback is what cracks the magic gate)"; exit 1
fi
