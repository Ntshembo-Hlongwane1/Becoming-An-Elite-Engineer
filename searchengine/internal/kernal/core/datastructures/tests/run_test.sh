#!/usr/bin/env bash

# Exit immediately if a command fails
set -e

# Ensure MinGW and CMake tools on D: drive are in PATH
export PATH="/d/mingw64/bin:/d/cmake/bin:$PATH"

# Check if test file argument was provided
if [ -z "$1" ]; then
    echo "Error: No test name provided."
    echo "Usage: ./run_test.sh <test_name>"
    echo "Example: ./run_test.sh bstree_test"
    echo "         ./run_test.sh bstree_test.cpp"
    exit 1
fi

# Strip .cpp extension if provided by user
TEST_NAME=$(basename "$1" .cpp)

# Resolve directory of this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_FILE="${SCRIPT_DIR}/${TEST_NAME}.cpp"
EXE_FILE="${SCRIPT_DIR}/${TEST_NAME}.exe"

# Check if the target .cpp file exists
if [ ! -f "$CPP_FILE" ]; then
    echo "Error: Test file '${CPP_FILE}' does not exist."
    echo "Available tests in tests folder:"
    for file in "${SCRIPT_DIR}"/*.cpp; do
        if [ -f "$file" ]; then
            echo "  - $(basename "$file" .cpp)"
        fi
    done
    exit 1
fi

echo "=========================================="
echo "Compiling ${TEST_NAME}.cpp..."
echo "=========================================="

g++ -std=c++17 -Wall -Wextra "${CPP_FILE}" -I"${SCRIPT_DIR}/.." -o "${EXE_FILE}"

echo "Build succeeded. Executing ${TEST_NAME}..."
echo "------------------------------------------"
"${EXE_FILE}"
echo "------------------------------------------"
echo "Test execution finished successfully!"
