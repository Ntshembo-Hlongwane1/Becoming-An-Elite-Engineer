#!/usr/bin/env bash
set -e

cd "$(dirname "${BASH_SOURCE[0]}")"

if [ -z "$1" ]; then
    echo "usage: ./run.sh <filename> [size=s|m|l]"
    exit 1
fi

mkdir -p bin
g++ -std=c++20 -O2 -g -Wall -Wextra src/main.cpp src/file_manager.cpp -o bin/file-manager
./bin/file-manager "$@"
