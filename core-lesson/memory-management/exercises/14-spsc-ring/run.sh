#!/usr/bin/env bash
# Build this exercise under ThreadSanitizer (not ASan) and run its tests.
#   ./run.sh            build + run all tests
#   ./run.sh foo        run only tests whose name contains "foo"
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build "$BUILD" -j"$(nproc)" >/dev/null
echo "──── running tests (ThreadSanitizer) ────"
export TSAN_OPTIONS="halt_on_error=1"   # stop at the first data race so it's obvious
set +e
"$BUILD/tests" "$@"
code=$?
set -e
echo "─────────────────────────────────────────"
if [[ $code -eq 0 ]]; then echo "ALL TESTS PASSED ✅ (TSan clean)"; else echo "TESTS FAILED (race or not implemented) ❌ — see above"; fi
exit $code
