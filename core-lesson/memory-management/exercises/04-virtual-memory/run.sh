#!/usr/bin/env bash
# Build this exercise (Debug: ASan+UBSan on) and run its tests.
#   ./run.sh            build + run all tests
#   ./run.sh codec      run only tests whose name contains "codec"
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build "$BUILD" -j"$(nproc)" >/dev/null
echo "──── running tests ────"
set +e
"$BUILD/tests" "$@"
code=$?
set -e
echo "───────────────────────"
if [[ $code -eq 0 ]]; then echo "ALL TESTS PASSED ✅"; else echo "TESTS FAILED (or not implemented) ❌  — see above"; fi
exit $code
