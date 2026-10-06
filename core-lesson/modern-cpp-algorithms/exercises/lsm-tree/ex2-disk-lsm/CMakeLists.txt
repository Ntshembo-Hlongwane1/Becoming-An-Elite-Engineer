cmake_minimum_required(VERSION 3.20)
project(disklsm CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LSM_TSAN "Build with ThreadSanitizer instead of ASan/UBSan" OFF)
add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(LSM_TSAN)
    add_compile_options(-fsanitize=thread -g -O1)
    add_link_options(-fsanitize=thread)
  else()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

# Exercise 1 is a dependency: its formats (blocks, varints, CRC, bloom, memtable, iterators) are
# reused unchanged. That reuse is the point: the on-disk engine only changes WHERE bytes live.
set(EX1_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../ex1-inmemory-lsm)
file(GLOB MINILSM_SOURCES CONFIGURE_DEPENDS ${EX1_DIR}/src/*.cpp)
add_library(minilsm STATIC ${MINILSM_SOURCES})
target_include_directories(minilsm PUBLIC ${EX1_DIR}/include)

file(GLOB DLSM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(disklsm STATIC ${DLSM_SOURCES})
target_include_directories(disklsm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(disklsm PUBLIC minilsm)

find_package(Threads REQUIRED)
file(GLOB DLSM_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
add_executable(dtests ${DLSM_TESTS})
target_link_libraries(dtests PRIVATE disklsm Threads::Threads)
target_include_directories(dtests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests ${EX1_DIR}/tests)

add_executable(lsmdump ${CMAKE_CURRENT_SOURCE_DIR}/tools/lsmdump.cpp)
target_link_libraries(lsmdump PRIVATE disklsm)

enable_testing()
add_test(NAME disklsm_tests COMMAND dtests)
