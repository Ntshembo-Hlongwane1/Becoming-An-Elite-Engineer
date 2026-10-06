cmake_minimum_required(VERSION 3.20)
project(minilsm CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LSM_TSAN "Build with ThreadSanitizer instead of ASan/UBSan" OFF)

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(LSM_TSAN)
    add_compile_options(-fsanitize=thread -g -O1)
    add_link_options(-fsanitize=thread)
  else()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

file(GLOB MINILSM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(minilsm STATIC ${MINILSM_SOURCES})
target_include_directories(minilsm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
file(GLOB MINILSM_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
add_executable(tests ${MINILSM_TESTS})
target_link_libraries(tests PRIVATE minilsm Threads::Threads)
target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)

enable_testing()
add_test(NAME minilsm_tests COMMAND tests)
