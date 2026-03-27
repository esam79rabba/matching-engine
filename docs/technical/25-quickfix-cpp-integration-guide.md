# QuickFIX C++ Integration Guide — FR-1

**Status**: Working implementation assumption — not an irrevocable procurement decision
**Decision**: D-01 (C++) CLOSED; QuickFIX C++ selected as FR-1 FIX engine (working assumption)
**Scope**: FIX 4.4 order entry + drop copy for FR-1

---

## 1. Recommended Repo Placement

```
matching-engine/
├── third_party/
│   └── quickfix/                   # QuickFIX C++ source (Git submodule)
├── gateway/
│   ├── fix-gateway/
│   │   ├── CMakeLists.txt          # Links against QuickFIX
│   │   ├── include/gateway/
│   │   │   ├── fix_application.h   # Our Application impl
│   │   │   ├── fix_adapter.h       # Adapter boundary (isolates QuickFIX types)
│   │   │   └── fix_session_mgr.h   # Session state management
│   │   ├── src/
│   │   │   ├── fix_application.cpp
│   │   │   ├── fix_adapter.cpp
│   │   │   └── fix_session_mgr.cpp
│   │   ├── config/
│   │   │   └── fix_session.cfg     # QuickFIX session config
│   │   └── test/
│   │       └── fix_adapter_test.cpp
```

## 2. Dependency Strategy

**Git submodule** (recommended for FR-1):

```bash
git submodule add https://github.com/quickfix/quickfix.git third_party/quickfix
```

**CMake integration** (`gateway/fix-gateway/CMakeLists.txt`):

```cmake
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/quickfix quickfix_build)

add_library(fix-gateway
    src/fix_application.cpp
    src/fix_adapter.cpp
    src/fix_session_mgr.cpp
)

target_include_directories(fix-gateway PUBLIC include)
target_link_libraries(fix-gateway
    PRIVATE quickfix       # QuickFIX library
    PRIVATE protocol       # Our SBE/internal protocol types
    PRIVATE engine-types   # engine::types.h
)
```

## 3. Adapter Boundary (Critical Design Pattern)

**Rule**: QuickFIX types (`FIX::Message`, `FIX::Session`, `FIX::FieldMap`) must NEVER leak into business logic or the engine. All interaction goes through an adapter that translates between QuickFIX types and our internal types.

```cpp
// fix_adapter.h — boundary between QuickFIX and our domain

#pragma once
#include "engine/types.h"
#include <quickfix/Message.h>
#include <optional>

namespace gateway {

struct InternalNewOrder {
    engine::ClientOrderId client_order_id;
    engine::InstrumentId  instrument_id;
    engine::Side          side;
    engine::OrderType     order_type;
    engine::TimeInForce   time_in_force;
    engine::Price         price;
    engine::Quantity      quantity;
    uint32_t              stp_group_id;
    uint16_t              flags;
};

struct InternalCancelRequest {
    engine::OrderId       orig_order_id;
    engine::ClientOrderId client_order_id;
    engine::InstrumentId  instrument_id;
};

class FixAdapter {
public:
    /// Translate FIX NewOrderSingle (D) → internal format.
    /// Returns nullopt if message is malformed.
    static std::optional<InternalNewOrder>
    parse_new_order(const FIX::Message& msg);

    /// Translate FIX OrderCancelRequest (F) → internal format.
    static std::optional<InternalCancelRequest>
    parse_cancel(const FIX::Message& msg);

    /// Build FIX ExecutionReport (8) from internal execution event.
    static FIX::Message
    build_execution_report(/* internal execution event params */);

    /// Build FIX OrderCancelReject (9) from internal reject.
    static FIX::Message
    build_cancel_reject(/* internal reject params */);
};

}  // namespace gateway
```

**Why this matters**: If we later replace QuickFIX with a commercial FIX engine or custom implementation, only the adapter layer changes. Business logic, engine, risk, and protocol modules are untouched.

## 4. Session Configuration Template

File: `gateway/fix-gateway/config/fix_session.cfg`

```ini
[DEFAULT]
ConnectionType=acceptor
ReconnectInterval=30
SenderCompID=VENUE
FileStorePath=./store
FileLogPath=./log
StartTime=00:00:00
EndTime=00:00:00
HeartBtInt=30
UseDataDictionary=Y
DataDictionary=./FIX44.xml
ValidateFieldsOutOfOrder=N
ResetOnLogon=Y
ResetOnLogout=Y
ResetOnDisconnect=Y

[SESSION]
BeginString=FIX.4.4
TargetCompID=BROKER_A
SocketAcceptPort=9876
```

**Notes**:
- `ResetOnLogon=Y`: Sequence numbers reset on each logon (FR-1 default; configurable)
- `HeartBtInt=30`: 30-second heartbeat (standard FIX default)
- One `[SESSION]` block per participant session
- Custom data dictionary may be needed for venue-specific tags (20001–20006)

## 5. First Logon Test Steps

1. Build the fix-gateway module: `cmake --build build --target fix-gateway`
2. Start the acceptor: `./fix_gateway --config config/fix_session.cfg`
3. Use a FIX client (e.g., `quickfix/examples/executor` or custom test client) to send Logon
4. Verify: gateway logs show session established, heartbeats exchanged
5. Send a `NewOrderSingle(D)` with minimal fields
6. Verify: gateway parses message via `FixAdapter`, logs internal format

## 6. What to Isolate Behind the Adapter

| Isolate (behind adapter) | Do NOT expose to business logic |
|--------------------------|--------------------------------|
| FIX message parsing | `FIX::Message` types |
| FIX session management | `FIX::Session` state |
| FIX sequence numbers | QuickFIX's internal sequence tracking |
| FIX data dictionary | XML schema details |
| FIX field accessors | `getField()`, `setField()` calls |

| Expose to business logic | Via |
|--------------------------|-----|
| `InternalNewOrder` | `FixAdapter::parse_new_order()` |
| `InternalCancelRequest` | `FixAdapter::parse_cancel()` |
| Session events (logon/logout/disconnect) | `FixSessionManager` callbacks |

## 7. Likely Risks / Gotchas

| Risk | Mitigation |
|------|-----------|
| QuickFIX C++ is less maintained than QuickFIX/J | Pin a stable release tag; vendor the source; maintain minimal patches |
| FIX 5.0SP2 support is limited in QuickFIX C++ | FR-1 uses FIX 4.4 only; evaluate alternatives for Phase 2 if 5.0SP2 needed |
| Custom tags require data dictionary modification | Create venue-specific `FIX44_VENUE.xml` extending the standard dictionary |
| Thread model mismatch | QuickFIX has its own thread for socket I/O; adapter must marshal to our internal queues safely |
| Memory allocation in QuickFIX | QuickFIX allocates on the heap (standard C++); this is acceptable in the gateway (not on engine hot path) |
| Config file management | Session configs will grow with participants; consider generating from reference data service |
