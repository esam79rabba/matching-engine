# C++ Engine Bootstrap — Implementation Initiation

**Status**: Active — based on closed decision D-01 (C++)
**Build system**: CMake
**Standard**: C++20
**Compiler targets**: GCC 13+ / Clang 17+

---

## Repository Bootstrap (Immediate)

### CMake Project Structure

```
matching-engine/
├── CMakeLists.txt                    # Top-level CMake
├── cmake/
│   ├── CompilerSettings.cmake        # Warnings, optimization flags
│   ├── Testing.cmake                 # Google Test setup
│   └── Sanitizers.cmake              # ASan, TSan, UBSan
├── engine/
│   ├── CMakeLists.txt
│   ├── core/
│   │   ├── CMakeLists.txt
│   │   ├── include/engine/
│   │   │   ├── order.h
│   │   │   ├── order_book.h
│   │   │   ├── price_level.h
│   │   │   ├── order_pool.h
│   │   │   ├── matching.h
│   │   │   ├── partition.h
│   │   │   └── types.h
│   │   ├── src/
│   │   │   ├── order_book.cpp
│   │   │   ├── price_level.cpp
│   │   │   ├── order_pool.cpp
│   │   │   ├── matching.cpp
│   │   │   └── partition.cpp
│   │   └── test/
│   │       ├── order_book_test.cpp
│   │       ├── matching_test.cpp
│   │       ├── order_pool_test.cpp
│   │       └── price_level_test.cpp
│   ├── sequencer/
│   │   ├── CMakeLists.txt
│   │   ├── include/engine/sequencer.h
│   │   ├── src/sequencer.cpp
│   │   └── test/sequencer_test.cpp
│   └── journal/
│       ├── CMakeLists.txt
│       ├── include/engine/journal.h          # Abstract interface
│       ├── include/engine/journal_local.h    # Custom NVMe impl
│       ├── include/engine/journal_memory.h   # In-memory stub
│       ├── src/journal_local.cpp
│       ├── src/journal_memory.cpp
│       └── test/journal_test.cpp
├── protocol/
│   ├── CMakeLists.txt
│   ├── include/protocol/
│   │   ├── sbe_messages.h
│   │   ├── internal_events.h
│   │   └── fix_mapping.h
│   └── src/
│       └── sbe_codec.cpp
├── gateway/
│   ├── fix-gateway/
│   │   └── CMakeLists.txt                    # Links QuickFIX C++
│   ├── binary-gateway/
│   │   └── CMakeLists.txt
│   └── shared/
│       ├── include/gateway/session.h
│       └── src/session.cpp
├── risk/
│   ├── CMakeLists.txt
│   ├── include/risk/
│   │   ├── risk_check.h                      # Abstract interface
│   │   ├── fat_finger.h
│   │   └── kill_switch.h
│   └── src/
│       ├── fat_finger.cpp
│       └── kill_switch.cpp
├── test/
│   ├── simulation/
│   │   └── replay_test.cpp                   # Deterministic replay
│   └── integration/
│       └── order_lifecycle_test.cpp           # E2E test
└── third_party/
    ├── quickfix/                              # QuickFIX C++ (submodule or vendored)
    └── googletest/                            # Google Test (submodule)
```

### Top-Level CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(matching-engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler settings
include(cmake/CompilerSettings.cmake)
include(cmake/Testing.cmake)
include(cmake/Sanitizers.cmake)

# Subdirectories
add_subdirectory(protocol)
add_subdirectory(engine)
add_subdirectory(gateway)
add_subdirectory(risk)
add_subdirectory(test)
```

### CompilerSettings.cmake

```cmake
# Strict warnings
add_compile_options(
    -Wall -Wextra -Wpedantic -Werror
    -Wno-unused-parameter           # Allow in interface stubs
    -fno-exceptions                  # No exceptions on hot path
    -fno-rtti                        # No RTTI on hot path
)

# Optimization
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -march=native -flto)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-O0 -g -fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()
```

---

## Core Type Definitions (types.h)

```cpp
#pragma once
#include <cstdint>

namespace engine {

using OrderId       = uint64_t;
using ClientOrderId = uint64_t;
using InstrumentId  = uint32_t;
using ParticipantId = uint32_t;
using AccountId     = uint32_t;
using PartitionId   = uint16_t;
using SequenceNum   = uint64_t;
using Timestamp     = uint64_t;  // nanoseconds since epoch (UTC)
using Price         = int64_t;   // integer-encoded (price × multiplier)
using Quantity      = uint64_t;  // integer-encoded (qty × multiplier)
using TradeId       = uint64_t;
using ExecId        = uint64_t;

enum class Side : uint8_t {
    Buy  = 1,
    Sell = 2
};

enum class OrderType : uint8_t {
    Market    = 1,
    Limit     = 2,
    Stop      = 3,  // Phase 2
    StopLimit = 4   // Phase 2
};

enum class TimeInForce : uint8_t {
    Day         = 0,
    GTC         = 1,
    GTD         = 2,
    IOC         = 3,
    FOK         = 4,
    AuctionOnly = 5  // Phase 2
};

enum class OrderStatus : uint8_t {
    New             = 0,
    PartiallyFilled = 1,
    Filled          = 2,
    Cancelled       = 3,
    Rejected        = 4,
    Expired         = 5
};

enum class InstrumentState : uint8_t {
    Inactive       = 0,
    PreOpen        = 1,
    Auction        = 2,  // Phase 2 (stretch)
    Continuous     = 3,
    Halt           = 4,
    Suspended      = 5,
    ClosingAuction = 6,  // Phase 2 (stretch)
    Closed         = 7,
    PostClose      = 8
};

enum class STPMode : uint8_t {
    CancelNewest = 1,  // FR-1
    CancelOldest = 2,  // Phase 2
    CancelBoth   = 3,  // Phase 2
    Decrement    = 4   // Phase 2
};

enum class ResidualPolicy : uint8_t {
    Cancel              = 1,  // Default
    MarketToLimitCollar = 2   // With protection collar
};

enum class RejectCode : uint16_t {
    UnknownInstrument    = 1,
    InvalidOrderType     = 2,
    PriceOutsideBands    = 3,
    QuantityExceedsMax   = 4,
    InvalidTick          = 5,
    InvalidLot           = 6,
    DuplicateClientOrder = 7,
    RateLimitExceeded    = 8,
    RiskCheckFailed      = 9,
    AccountNotFound      = 10,
    InstrumentHalted     = 11,
    FOKInsufficientQty   = 12,
    PostOnlyWouldMatch   = 13,
    MarketResidualCancel = 14,
    CollarOutsideBands   = 15,
    STPCancelled         = 16,
    IOCResidualCancel    = 17,
    InternalError        = 99
};

// Order flags (bitfield)
constexpr uint16_t FLAG_POST_ONLY           = 0x01;
constexpr uint16_t FLAG_REDUCE_ONLY         = 0x02;
constexpr uint16_t FLAG_CANCEL_ON_DISCONNECT = 0x04;

}  // namespace engine
```

---

## First Day Tasks (Repo Creation)

| # | Task | Output | Time |
|---|------|--------|------|
| 1 | Create Git repo with branch protection | Repository live | 30 min |
| 2 | Add CMakeLists.txt + cmake/ directory | Build system compiles empty project | 1 hour |
| 3 | Add `engine/core/include/engine/types.h` | Core type definitions | 30 min |
| 4 | Add Google Test submodule + first empty test | `ctest` runs and passes | 30 min |
| 5 | Set up GitHub Actions CI (build + test) | CI green on push | 1 hour |
| 6 | Add `.clang-format` and `.clang-tidy` configs | Formatting enforced | 30 min |
| 7 | Add README.md, ARCHITECTURE.md, CONTRIBUTING.md | Documentation skeleton | 30 min |
| 8 | Copy protocol drafts + ADRs into `docs/` | Design artifacts in repo | 15 min |

**Total Day 1 effort**: ~5 hours. Repository is build-ready by end of Day 1.

---

## C++ Coding Standards (Engine-Specific)

| Rule | Detail |
|------|--------|
| **No heap allocation on hot path** | All Order, PriceLevel, Event objects from pre-allocated pools |
| **No exceptions** | `-fno-exceptions` on engine modules; use return codes or `std::expected` (C++23) |
| **No RTTI** | `-fno-rtti` on engine modules |
| **No virtual dispatch on hot path** | Templates / CRTP for polymorphism where needed |
| **No STL containers on hot path** | Custom containers with pre-allocated backing storage |
| **Integer arithmetic only** | No floating-point in matching logic; all prices/quantities as integers |
| **Cache-friendly layout** | Prefer struct-of-arrays for hot fields; keep Order size ≤ 128 bytes (2 cache lines) |
| **Const correctness** | All non-mutating methods and parameters `const` |
| **Naming**: snake_case | Functions, variables, namespaces: `snake_case`; types: `PascalCase`; constants: `UPPER_SNAKE` |
| **Header guards** | `#pragma once` (all major compilers support it) |

---

## Hiring (C++ Specific)

### Immediate Job Postings

| Role | Count | Key Requirements |
|------|:-----:|-----------------|
| Senior C++ Engine Engineer | 2–3 | C++17/20; low-latency systems; exchange/HFT/financial experience preferred |
| C++ Gateway Engineer | 1 | C++17; networking (TCP, possibly DPDK later); FIX protocol experience |
| C++ Performance Engineer | 1 | Profiling (perf, VTune); cache optimization; lock-free data structures |

### Job Description Key Points
- Modern C++ (17/20); no legacy C-style code
- Low-latency, zero-allocation hot-path development
- Matching engine or adjacent financial systems experience strongly preferred
- Familiarity with kernel bypass networking (DPDK) is a plus for Phase 2+
- Test-driven development; deterministic replay testing
