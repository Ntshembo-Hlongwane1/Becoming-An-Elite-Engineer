cmake_minimum_required(VERSION 3.15)
project(searchengine LANGUAGES CXX)

# Require C++20 standard for modern features
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Use -std=c++20 rather than -std=gnu++20, so anything that only builds because
# of a GNU extension fails here instead of on a stricter compiler later.
set(CMAKE_CXX_EXTENSIONS OFF)

# Automatically find all C++ source files (.cpp) inside src/ and internal/ directories
# This mimics Go's behavior of automatically compiling/linking all files in a folder.
# cmd/ is deliberately excluded: it holds main(), and a library must not own an entry point.
file(GLOB_RECURSE CORE_SOURCES
    "src/*.cpp"
    "internal/*.cpp"
)
# Exclude test files so they don't conflict with main() in cmd/main.cpp
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/tests/.*")

# All engine logic lives in one static library so that more than one executable
# (the app, and the benchmark harness) can link against it.
add_library(searchengine_core STATIC ${CORE_SOURCES})

# Include directory setup:
# This allows you to write "#include <internal/someheader.hpp>"
# relative to this CMakeLists.txt directory without complex relative paths (like "../../").
# PUBLIC so that anything linking the library inherits the same include root.
target_include_directories(searchengine_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Threads REQUIRED)
target_link_libraries(searchengine_core PUBLIC Threads::Threads)

# Define the final executable output. The entry point is cmd/main.cpp.
add_executable(searchengine cmd/main.cpp)
target_link_libraries(searchengine PRIVATE searchengine_core)

# ---------------------------------------------------------------------------
# Benchmarks (opt-in: -DBUILD_BENCHMARKS=ON)
#
# Off by default so a normal build stays fast and needs no network access.
# ---------------------------------------------------------------------------
option(BUILD_BENCHMARKS "Build the Google Benchmark harness" OFF)

if(BUILD_BENCHMARKS)
    include(FetchContent)

    # Google Benchmark builds its own test suite and pulls GoogleTest unless told not to.
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    # MinGW emits warnings inside benchmark's own sources; don't let them fail the build.
    set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.9.1
    )
    FetchContent_MakeAvailable(googlebenchmark)

    file(GLOB BENCHMARK_SOURCES "benchmarks/*.cpp")

    add_executable(searchengine_bench ${BENCHMARK_SOURCES})
    target_link_libraries(searchengine_bench
        PRIVATE
            searchengine_core
            benchmark::benchmark_main
    )

    if(WIN32)
        # psapi: GetProcessMemoryInfo, for the memory counters.
        target_link_libraries(searchengine_bench PRIVATE psapi)
    endif()
endif()
