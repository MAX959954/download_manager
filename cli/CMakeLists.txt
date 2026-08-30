cmake_minimum_required(VERSION 3.21)
project(download_manager
    VERSION 0.1.0
    LANGUAGE CXX
)

set (CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_OUTPUT_EXTENSION OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# Общие варнинги — INTERFACE-либа, подключается к каждому нашему таргету.
add_library(dlm_warnings INTERFACE)
if (MSVC)
    target_compile_options(dlm_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(dlm_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(core)
add_subdirectory(cli)