cmake_minimum_required(VERSION 3.15)
project(searchengine LANGUAGES CXX)

# Require C++17 standard for modern features
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Automatically find all C++ source files (.cpp) inside cmd/ and src/ directories
# This mimics Go's behavior of automatically compiling/linking all files in a folder
file(GLOB_RECURSE SOURCES 
    "cmd/*.cpp"
    "src/*.cpp"
    "internal/*.cpp"
)

# Define the final executable output and its source files
# The entry point is cmd/main.cpp, which is included in the SOURCES list above
add_executable(searchengine ${SOURCES})

# Include directory setup:
# This allows you to write "#include <src/someheader.h>" or "#include <cmd/someheader.h>"
# relative to this CMakeLists.txt directory without complex relative paths (like "../../")
target_include_directories(searchengine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
