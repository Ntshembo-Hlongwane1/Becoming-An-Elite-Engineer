cmake_minimum_required(VERSION 3.20)
project(mm_malloc CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()
add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)

# Sanitizers are applied to the allocator + tests only (below), NOT globally: the bench must run
# against the REAL glibc malloc, which ASan would otherwise intercept (and whose intentional
# leak LeakSanitizer would flag).
set(MM_SAN $<$<CONFIG:Debug>:-fsanitize=address,undefined>
           $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
           $<$<CONFIG:Debug>:-g>)

file(GLOB MM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(mm STATIC ${MM_SOURCES})
target_include_directories(mm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(mm PUBLIC ${MM_SAN})
target_link_options(mm PUBLIC ${MM_SAN})

file(GLOB MM_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
add_executable(tests ${MM_TESTS})
target_link_libraries(tests PRIVATE mm)   # inherits MM_SAN transitively
target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)

# bench/ uses the REAL malloc to reproduce the Lesson 7.1 measurements; NO sanitizers, NO mm.
add_executable(heap_probe ${CMAKE_CURRENT_SOURCE_DIR}/bench/heap_probe.cpp)

enable_testing()
add_test(NAME mm_tests COMMAND tests)
