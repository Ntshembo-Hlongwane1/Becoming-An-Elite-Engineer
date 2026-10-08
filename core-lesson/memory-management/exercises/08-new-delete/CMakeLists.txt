cmake_minimum_required(VERSION 3.20)
project(mm_new_delete CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()
add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()
# A global `operator new` replacement must land in the final EXECUTABLE to take effect, so the src/
# files are compiled straight into the test binary (not via a static library, from which the linker
# might not pull the replacement objects). Lesson 8.4.
file(GLOB MM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB MM_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
add_executable(tests ${MM_TESTS} ${MM_SOURCES})
target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/tests)
# bench/ is a standalone demo with its OWN logging operators (Lesson 8.1 measurements); it must NOT
# include the counting operators from src/, so it is its own executable.
add_executable(newdelete_probe ${CMAKE_CURRENT_SOURCE_DIR}/bench/newdelete_probe.cpp)

enable_testing()
add_test(NAME mm_tests COMMAND tests)
