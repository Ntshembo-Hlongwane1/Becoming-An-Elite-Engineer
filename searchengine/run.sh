#!/usr/bin/env bash

# Exit immediately if a command exits with a non-zero status
set -e

echo "=== 1. Setting up Environment Variables ==="
# Ensure D Drive paths for MinGW and CMake are active in the script session
export PATH="/d/mingw64/bin:/d/cmake/bin:$PATH"

# Make sure we are in the correct directory (directory of the script)
cd "$(dirname "$0")"

# 2. Always regenerate build files so GLOB_RECURSE picks up new source files
echo "=== 2. Generating CMake Build Files ==="
cmake -S . -B build -G "MinGW Makefiles"

echo "=== 3. Compiling C++ Project ==="
cmake --build build

echo "=== 4. Running Search Engine Executable ==="
echo "-------------------------------------------"
./build/searchengine.exe
echo "-------------------------------------------"
