cmake_minimum_required(VERSION 3.20)
project(aligned_memory CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  # Sanitizers always on in Debug (notes Part 3 §8): every mismatched free, overflow and misaligned
  # access in these exercises is caught here, not in production.
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

file(GLOB AM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(am STATIC ${AM_SOURCES})
target_include_directories(am PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

file(GLOB AM_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
add_executable(tests ${AM_TESTS})
target_link_libraries(tests PRIVATE am)
target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)

# Exercise 6: your rewritten drill / benchmark. Build it in Release for real numbers:
#   cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel --target drill_bench
add_executable(drill_bench ${CMAKE_CURRENT_SOURCE_DIR}/bench/drill_bench.cpp)
target_link_libraries(drill_bench PRIVATE am)

enable_testing()
add_test(NAME am_tests COMMAND tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
