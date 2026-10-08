cmake_minimum_required(VERSION 3.20)
project(mm_virtual_memory CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Warnings that catch exactly the bugs this lesson is about (§1.4): signed/unsigned mixing and
# silent narrowing. Not -Werror, so your own debugging builds aren't blocked — but keep them clean.
add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wsign-compare)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

file(GLOB MM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(mm STATIC ${MM_SOURCES})
target_include_directories(mm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

file(GLOB MM_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
add_executable(tests ${MM_TESTS})
target_link_libraries(tests PRIVATE mm)
target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)

enable_testing()
add_test(NAME mm_tests COMMAND tests)
