#!/bin/bash

# Codeforces Runner
# Usage: ./run.sh <problem_code>
# Example: ./run.sh 750A
#          ./run.sh 750A < input.txt

if [ -z "$1" ]; then
    echo "Usage: ./run.sh <problem_code>"
    echo "Example: ./run.sh 750A"
    exit 1
fi

CODE="${1%/}"  # allow tab-completed "750A/"
SRC_DIR="$(dirname "$0")/${CODE}"

if [ ! -d "$SRC_DIR" ]; then
    echo "Error: folder $SRC_DIR not found!"
    exit 1
fi

# Find the source file (either .cpp or .cc)
SRC_FILE=""
for ext in cpp cc; do
    if [ -f "${SRC_DIR}/${CODE}.${ext}" ]; then
        SRC_FILE="${SRC_DIR}/${CODE}.${ext}"
        break
    fi
done

if [ -z "$SRC_FILE" ]; then
    echo "Error: no ${CODE}.cpp or ${CODE}.cc in $SRC_DIR"
    exit 1
fi

EXE_FILE="${SRC_DIR}/${CODE}.exe"

# Always a fresh build: never reuse an old binary or any compiler cache
rm -f "$EXE_FILE"
export CCACHE_DISABLE=1

echo "Compiling $(basename "$SRC_FILE") (fresh build)..."
if ! /usr/bin/g++ -std=c++20 -O2 -Wall -Wextra -o "$EXE_FILE" "$SRC_FILE"; then
    echo "Compilation failed!"
    exit 1
fi

echo "Running ${CODE}..."
echo "-------------------"
"$EXE_FILE"
