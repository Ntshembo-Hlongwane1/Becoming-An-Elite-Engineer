#!/usr/bin/env bash

# Exit immediately if a command exits with a non-zero status
set -e

usage() {
    cat <<'EOF'
Usage:
  ./run.sh --live                     Build and run the search engine normally
  ./run.sh --benchmark cold           Benchmark startup with no search index (must build one)
  ./run.sh --benchmark warm           Benchmark startup with the search index already present

Any extra arguments after the mode are forwarded to the benchmark binary, e.g.
  ./run.sh --benchmark cold --benchmark_repetitions=3
EOF
}

# ===== Parse arguments =====
MODE="${1:-}"

case "$MODE" in
    --live)
        shift
        ;;
    --benchmark)
        shift
        BENCH_MODE="${1:-}"
        if [ "$BENCH_MODE" != "cold" ] && [ "$BENCH_MODE" != "warm" ]; then
            echo "Error: --benchmark requires 'cold' or 'warm'" >&2
            usage >&2
            exit 1
        fi
        shift
        ;;
    *)
        usage >&2
        exit 1
        ;;
esac

echo "=== 1. Terminating Previous Running Instances ==="
# Kill any existing instance of the executables if running
taskkill //F //IM searchengine.exe 2>/dev/null || true
taskkill //F //IM searchengine_bench.exe 2>/dev/null || true

echo "=== 2. Setting up Environment Variables ==="
# Ensure D Drive paths for MinGW and CMake are active in the script session
export PATH="/d/mingw64/bin:/d/cmake/bin:$PATH"

# Make sure we are in the correct directory (directory of the script)
cd "$(dirname "$0")"

if [ "$MODE" = "--live" ]; then
    echo "=== 3. Cleaning Build Directory for Fresh Compile ==="
    rm -rf build

    echo "=== 4. Generating CMake Build Files ==="
    cmake -S . -B build -G "MinGW Makefiles"

    echo "=== 5. Compiling C++ Project ==="
    cmake --build build

    echo "=== 6. Running Search Engine Executable ==="
    echo "-------------------------------------------"
    ./build/searchengine.exe
    echo "-------------------------------------------"
else
    # Benchmarks live in their own build tree so that switching modes does not
    # force a reconfigure, and so the fetched Google Benchmark sources are not
    # re-downloaded on every run. It is NOT wiped for that reason.
    echo "=== 3. Generating CMake Build Files (Release + benchmarks) ==="
    cmake -S . -B build_bench -G "MinGW Makefiles" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_BENCHMARKS=ON

    echo "=== 4. Compiling Benchmark Harness ==="
    cmake --build build_bench --target searchengine_bench

    # cold -> BM_Startup_Cold, warm -> BM_Startup_Warm
    if [ "$BENCH_MODE" = "cold" ]; then
        FILTER="BM_Startup_Cold"

        # A true cold run has to evict the OS file cache before every
        # iteration, which is a privileged operation. Check up front rather
        # than letting the benchmark discover it and abort.
        if ! net session >/dev/null 2>&1; then
            echo "Error: 'cold' needs Administrator rights to evict the OS file cache." >&2
            echo "       Without it, only the first run after a reboot reads from disk;" >&2
            echo "       every later run is served from RAM and reports a false result." >&2
            echo "       Re-run this script from an elevated terminal." >&2
            exit 1
        fi
    else
        FILTER="BM_Startup_Warm"
    fi

    echo "=== 5. Running '$BENCH_MODE' Startup Benchmark ==="
    echo "-------------------------------------------"
    # Run from the project root so DirectoryReader resolves ./data
    ./build_bench/searchengine_bench.exe --benchmark_filter="$FILTER" "$@"
    echo "-------------------------------------------"
fi
