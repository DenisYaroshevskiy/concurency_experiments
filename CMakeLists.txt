# Copyright 2025 Denis Yaroshevskiy
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)

cmake_minimum_required(VERSION 3.15)

project(concurrency_experiments)


SET(CMAKE_EXPORT_COMPILE_COMMANDS ON)
SET(CMAKE_CXX_STANDARD 23)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif(NOT CMAKE_BUILD_TYPE)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories("./include")
include_directories("../relacy")

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_SOURCE_DIR}/../benchmark" "${CMAKE_BINARY_DIR}/benchmark_build")

enable_testing()
add_subdirectory("./rl_tests")
