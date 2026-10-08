#!/usr/bin/env bash
# Usage: ./run.sh <N> [args...]   -> builds and runs drill<N>.cpp
set -euo pipefail

if [[ $# -lt 1 || ! "$1" =~ ^[0-9]+$ ]]; then
    echo "usage: $0 <drill-number> [args...]" >&2
    exit 1
fi

N="$1"; shift
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build"
TARGET="drill$N"

if [[ ! -f "$ROOT/$TARGET.cpp" ]]; then
    echo "error: $TARGET.cpp not found in $ROOT" >&2
    exit 1
fi

cmake -S "$ROOT" -B "$BUILD" >/dev/null
cmake --build "$BUILD" --target "$TARGET" --clean-first -j"$(nproc)"

echo "──── running $TARGET ────"
exec "$BUILD/$TARGET" "$@"
