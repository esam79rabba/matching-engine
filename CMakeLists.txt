cmake_minimum_required(VERSION 3.20)
project(matching-engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─── Compiler settings ─────────────────────────────────────────
add_compile_options(
    -Wall -Wextra -Wpedantic -Werror
    -Wno-unused-parameter
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -march=native -flto)
    add_link_options(-flto)
endif()

option(ENABLE_SANITIZERS "Enable ASan + UBSan" OFF)
if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# ─── Google Test ───────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
enable_testing()

# ─── Subdirectories ────────────────────────────────────────────
add_subdirectory(common)
add_subdirectory(engine/core)
add_subdirectory(risk)
add_subdirectory(gateway/fix)
add_subdirectory(gateway/sbe)
add_subdirectory(market-data)
add_subdirectory(dropcopy)
add_subdirectory(refdata)
add_subdirectory(ops)
add_subdirectory(eod)
add_subdirectory(benchmark)
add_subdirectory(certification)
add_subdirectory(replication)
add_subdirectory(simulator)
add_subdirectory(daemon)
