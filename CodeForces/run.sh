#!/bin/bash

# Codeforces Runner
# Usage: bash run.sh <problem_code>
# Example: bash run.sh 71A

if [ -z "$1" ]; then
    echo "Usage: bash run.sh <problem_code>"
    echo "Example: bash run.sh 71A"
    exit 1
fi

CODE="$1"
SRC_DIR="$(dirname "$0")/${CODE}"
SRC_FILE="${SRC_DIR}/${CODE}.cpp"
EXE_FILE="${SRC_DIR}/${CODE}.exe"

# Check if source file exists
if [ ! -f "$SRC_FILE" ]; then
    echo "Error: $SRC_FILE not found!"
    exit 1
fi

# Compile
echo "Compiling ${CODE}.cpp..."
g++ -std=c++17 -O2 -Wall -o "$EXE_FILE" "$SRC_FILE"

if [ $? -ne 0 ]; then
    echo "Compilation failed!"
    exit 1
fi

echo "Running ${CODE}..."
echo "-------------------"
"$EXE_FILE"
