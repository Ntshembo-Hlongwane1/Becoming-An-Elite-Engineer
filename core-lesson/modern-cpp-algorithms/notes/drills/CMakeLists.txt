cmake_minimum_required(VERSION 3.16)
project(drills CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Shared code: every .cpp under include/ is compiled once into a library
# that every drill links against.
file(GLOB_RECURSE DRILL_LIB_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/include/*.cpp)
add_library(drill_lib STATIC ${DRILL_LIB_SOURCES})
target_include_directories(drill_lib PUBLIC ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include)
target_compile_options(drill_lib PRIVATE -Wall -Wextra -Wpedantic)

# One executable per drill<N>.cpp -> target "drill<N>", main() lives in that file.
file(GLOB DRILL_ENTRIES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/drill*.cpp)
foreach(entry ${DRILL_ENTRIES})
    get_filename_component(name ${entry} NAME_WE)   # drill1.cpp -> drill1
    add_executable(${name} ${entry})
    target_link_libraries(${name} PRIVATE drill_lib)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()
