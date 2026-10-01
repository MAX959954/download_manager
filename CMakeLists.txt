cmake_minimum_required(VERSION 3.21)

project(download_manager
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# Shared warning flags — an INTERFACE lib, attached to every one of our targets.
add_library(dlm_warnings INTERFACE)
if (MSVC)
    target_compile_options(dlm_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(dlm_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(core)
add_subdirectory(cli)

enable_testing()
add_subdirectory(tests)

# Benchmark (1 vs 4 vs 8 threads x chunk sizes) — a separate, manually
# built target, not part of the regular build/ctest. See benchmarks/README
# via bench_downloader.cpp.
option(DLM_BUILD_BENCHMARKS "Build benchmark tools (bench_downloader)" OFF)
if (DLM_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
