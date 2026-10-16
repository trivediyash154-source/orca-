# ORCA — top-level build
#
# Phase 0 scaffold. Defines the target structure from ARCHITECTURE.md §8; the source
# lists are populated in Phase 1+ as modules are implemented.
#
# NOTE: CMake is not installed on the development machine as of the Phase 0 audit
# (see docs/REPOSITORY_AUDIT.md §3). This file has therefore NOT been configured or
# validated by running CMake. Treat it as an unverified scaffold until `cmake -B build`
# succeeds for the first time in Phase 1.

cmake_minimum_required(VERSION 3.20)

project(ORCA
    VERSION 0.1.0
    DESCRIPTION "Autonomous Orbital Rescue Mission Planning Using Multi-Objective Optimization"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(ORCA_BUILD_TESTS  "Build the test suite"       ON)
option(ORCA_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

# Shared compile options — applied via an interface target so every ORCA target
# gets identical flags without polluting global state.
add_library(orca_options INTERFACE)
target_compile_features(orca_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(orca_options INTERFACE /W4)
    if(ORCA_WARNINGS_AS_ERRORS)
        target_compile_options(orca_options INTERFACE /WX)
    endif()
else()
    target_compile_options(orca_options INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
        -Wunused -Woverloaded-virtual -Wdouble-promotion
    )
    if(ORCA_WARNINGS_AS_ERRORS)
        target_compile_options(orca_options INTERFACE -Werror)
    endif()
endif()

# Release must be optimised with assertions disabled — BENCHMARK_PLAN.md §2.4
# requires -O2 -DNDEBUG for every reported measurement.
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g")

add_subdirectory(cpp)

if(ORCA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "ORCA ${PROJECT_VERSION}")
message(STATUS "  build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  tests      : ${ORCA_BUILD_TESTS}")
