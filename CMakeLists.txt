cmake_minimum_required(VERSION 3.21)

project(jitter_buffer
    VERSION 0.1.0
    DESCRIPTION "Lock-free SPSC ring buffer, jitter buffer, and FEC packetizer for real-time audio"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(JITTER_BUFFER_IS_TOP_LEVEL ON)
else()
    set(JITTER_BUFFER_IS_TOP_LEVEL OFF)
endif()

option(JITTER_BUFFER_BUILD_TESTS "Build the test suite" ${JITTER_BUFFER_IS_TOP_LEVEL})
option(JITTER_BUFFER_BUILD_BENCHMARKS "Build the benchmarks" OFF)
set(JITTER_BUFFER_SANITIZER "" CACHE STRING
    "Sanitizers to enable: empty, or a semicolon list of address,undefined,thread")

# ---------------------------------------------------------------------------
# The library itself is header-only: just an interface target carrying the
# include path and the C++20 requirement.
# ---------------------------------------------------------------------------
add_library(jitter_buffer INTERFACE)
add_library(jitter::buffer ALIAS jitter_buffer)
target_include_directories(jitter_buffer INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(jitter_buffer INTERFACE cxx_std_20)

# ---------------------------------------------------------------------------
# Shared warning and sanitizer flags for the buildable targets (tests, benches).
# ---------------------------------------------------------------------------
add_library(jitter_project_options INTERFACE)
if(MSVC)
    target_compile_options(jitter_project_options INTERFACE /W4 /permissive- /EHsc)
else()
    target_compile_options(jitter_project_options INTERFACE -Wall -Wextra -Wpedantic)
endif()

if(JITTER_BUFFER_SANITIZER)
    set(_san_flags "")
    foreach(s IN LISTS JITTER_BUFFER_SANITIZER)
        list(APPEND _san_flags -fsanitize=${s})
    endforeach()
    target_compile_options(jitter_project_options INTERFACE ${_san_flags} -fno-omit-frame-pointer -g)
    target_link_options(jitter_project_options INTERFACE ${_san_flags})
endif()

if(JITTER_BUFFER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(JITTER_BUFFER_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
