#!/usr/bin/env bash

# Exit immediately if a command exits with a non-zero status
set -e

echo "=== 1. Terminating Previous Running Instances ==="
# Kill any existing instance of searchengine.exe if running
taskkill //F //IM searchengine.exe 2>/dev/null || true

echo "=== 2. Setting up Environment Variables ==="
# Ensure D Drive paths for MinGW and CMake are active in the script session
export PATH="/d/mingw64/bin:/d/cmake/bin:$PATH"

# Make sure we are in the correct directory (directory of the script)
cd "$(dirname "$0")"

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
