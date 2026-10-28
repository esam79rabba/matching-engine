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

# AppleClang 21+ added -Wcharacter-conversion which fires on GoogleTest headers.
# Scoped suppression so Linux/GCC/older Clang are unaffected.
if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    add_compile_options(-Wno-error=character-conversion)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -march=native -flto)
    add_link_options(-flto)
endif()

option(ENABLE_SANITIZERS "Enable ASan + UBSan" OFF)
if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# ─── OpenSSL (hard requirement) ────────────────────────────────
# Needed for SHA-256 audit chain, HMAC auth tokens and TLS transport.
# There is deliberately no non-cryptographic fallback.
find_package(OpenSSL 3.0 REQUIRED)
message(STATUS "OpenSSL ${OPENSSL_VERSION} found")
# Retained as a global define because several sources still gate their
# OpenSSL code paths on it; it is always set now.
add_compile_definitions(OPENSSL_AVAILABLE=1)

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

# ─── QuickFIX C++ (optional — FIX session layer) ─────────────
option(ENABLE_QUICKFIX "Build with QuickFIX C++ session layer" ON)
if(ENABLE_QUICKFIX)
    FetchContent_Declare(
        quickfix
        GIT_REPOSITORY https://github.com/quickfix/quickfix.git
        GIT_TAG master
    )
    set(QUICKFIX_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(QUICKFIX_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(QUICKFIX_TESTS OFF CACHE BOOL "" FORCE)

    # QuickFIX has an old cmake_minimum_required; work around it
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

    # QuickFIX FetchContent can fail on some platforms; make it non-fatal
    FetchContent_GetProperties(quickfix)
    if(NOT quickfix_POPULATED)
        FetchContent_Populate(quickfix)
        # QuickFIX needs its cmake/ dir on the module path
        list(APPEND CMAKE_MODULE_PATH "${quickfix_SOURCE_DIR}/cmake")
        if(EXISTS "${quickfix_SOURCE_DIR}/CMakeLists.txt")
            add_subdirectory(${quickfix_SOURCE_DIR} ${quickfix_BINARY_DIR} EXCLUDE_FROM_ALL)
            if(TARGET quickfix)
                # QuickFIX uses dynamic exception specifications (removed in C++17).
                # Suppress the error for the QuickFIX target only.
                target_compile_options(quickfix PRIVATE
                    -Wno-dynamic-exception-spec
                    -Wno-error=dynamic-exception-spec
                    -Wno-deprecated
                    -Wno-error
                    -Wno-unused-parameter
                    -Wno-extra
                    -Wno-pedantic
                )
                set(QUICKFIX_FOUND TRUE)
                add_compile_definitions(QUICKFIX_AVAILABLE)
                message(STATUS "QuickFIX C++ enabled — session layer available")
            else()
                set(QUICKFIX_FOUND FALSE)
                message(WARNING "QuickFIX target not created — falling back to custom FIX parser")
            endif()
        else()
            set(QUICKFIX_FOUND FALSE)
            message(WARNING "QuickFIX source not found — falling back to custom FIX parser")
        endif()
    endif()

    # Reset policy version
    unset(CMAKE_POLICY_VERSION_MINIMUM CACHE)
else()
    set(QUICKFIX_FOUND FALSE)
    message(STATUS "QuickFIX disabled — using custom FIX parser only")
endif()

# ─── Subdirectories ────────────────────────────────────────────
add_subdirectory(common)
add_subdirectory(engine/core)
add_subdirectory(risk)
add_subdirectory(surveillance)
add_subdirectory(post-trade)
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
