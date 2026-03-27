# Pre-Trade Risk Service Interface — FR-1 Specification

**Status**: Engineering draft — C++ oriented
**Decisions**: D-01 (C++) CLOSED
**Source**: §12.3, §14.5 (frozen baseline)

---

## Interface Position in Architecture

```
Gateway → [RiskService::check()] → Sequencer → Engine
                                         ↑
                              Risk rejects never reach the sequencer.
                              Approved orders proceed to sequencing.
```

The risk service sits on the ingress path **before** the sequencer. It is latency-sensitive but is NOT part of the deterministic engine hot path. It may use standard memory allocation and logging.

---

## Request / Response Model

**Synchronous, inline check.** The gateway calls `RiskService::check()` and blocks until the result is returned. This keeps the ingress pipeline simple and deterministic from the gateway's perspective.

**Latency budget**: < 5 µs per check (Profile A engineering target). The risk service must not introduce unbounded latency.

```cpp
namespace risk {

enum class RiskResult : uint8_t {
    PASS    = 0,
    REJECT  = 1
};

struct RiskRequest {
    engine::OrderId       order_id;          // 0 for new (not yet assigned)
    engine::ClientOrderId client_order_id;
    engine::ParticipantId participant_id;
    engine::AccountId     account_id;
    engine::InstrumentId  instrument_id;
    engine::Side          side;
    engine::OrderType     order_type;
    engine::Price         price;             // integer-encoded
    engine::Quantity      quantity;
    engine::Timestamp     gateway_recv_ts;   // NIC timestamp
    uint32_t              session_id;
};

struct RiskResponse {
    RiskResult            result;
    engine::RejectCode    reject_code;       // 0 if PASS
    // No text — reject_code is sufficient; text logged separately
};

// Abstract interface — allows stub for M1, real impl for production
class IRiskService {
public:
    virtual ~IRiskService() = default;

    // Synchronous check. Must complete within latency budget.
    virtual RiskResponse check(const RiskRequest& req) = 0;

    // Kill switch — cancel all orders for a scope.
    // Returns number of synthetic cancel messages generated.
    virtual uint32_t kill_switch(KillSwitchScope scope, uint32_t target_id) = 0;

    // Update limits at runtime (no restart).
    virtual void update_limits(engine::ParticipantId participant_id,
                               const RiskLimits& new_limits) = 0;
};

}  // namespace risk
```

---

## FR-1 Risk Checks

### Check 1: Fat-Finger Price

```
IF order.order_type == LIMIT:
    reference_price = get_reference_price(order.instrument_id)
    IF reference_price > 0:
        deviation_pct = abs(order.price - reference_price) / reference_price * 100
        IF deviation_pct > limits.max_order_price_pct:
            REJECT(PriceOutsideBands)
```

- Reference price: last trade price, or previous session close, or manual-set reference
- Configurable per instrument via `RiskLimits.max_order_price_pct`
- Market orders skip this check (they execute at whatever price is available; price bands in the engine provide secondary protection)

### Check 2: Max Quantity

```
IF order.quantity > limits.max_order_qty:
    REJECT(QuantityExceedsMax)
```

- Configurable per participant per instrument group

### Check 3: Max Notional

```
notional = order.price * order.quantity / price_multiplier
IF notional > limits.max_order_notional:
    REJECT(RiskCheckFailed)
```

- For market orders: use best opposite price as estimate; if no BBO, use reference price
- Configurable per participant

### Check 4: Message Rate Throttle

```
current_rate = session_rate_counter.increment_and_get(session_id)
IF current_rate > limits.max_message_rate:
    REJECT(RateLimitExceeded)
```

- Measured per session (not per participant — a participant may have multiple sessions)
- Sliding window or token-bucket implementation
- Configurable per session
- Separate counters for: total messages, new orders, cancels

### Check 5: Max Open Orders

```
open_count = get_open_order_count(participant_id, instrument_group)
IF open_count >= limits.max_open_orders:
    REJECT(RiskCheckFailed)
```

- Tracked per participant per instrument group
- Updated on fill/cancel events from the engine (async feedback loop)
- FR-1: approximate count (eventual consistency with engine state is acceptable)

---

## Risk Limits Structure

```cpp
namespace risk {

struct RiskLimits {
    engine::ParticipantId participant_id;
    uint32_t              instrument_group_id;  // 0 = ALL instruments
    double                max_order_price_pct;  // e.g., 10.0 = 10%
    engine::Quantity      max_order_qty;         // max per order
    double                max_order_notional;    // max notional per order (in quote currency)
    uint32_t              max_message_rate;       // per second per session
    uint32_t              max_new_order_rate;      // per second per session
    uint32_t              max_open_orders;         // resting orders per participant
    bool                  kill_switch_enabled;     // always true for FR-1
};

enum class KillSwitchScope : uint8_t {
    PARTICIPANT = 1,   // Cancel all orders for a participant
    INSTRUMENT  = 2,   // Cancel all orders for an instrument
    MARKET_WIDE = 3    // Cancel all orders venue-wide
};

}  // namespace risk
```

---

## Kill Switch Interaction

```
Operator Console ──→ RiskService::kill_switch(scope, target_id)
                         │
                         ▼
                    For each affected resting order:
                         Generate synthetic CancelOrder message
                         Route through sequencer (deterministic)
                         Engine processes cancel normally
                         │
                         ▼
                    Effect time target: < 1 second from activation
```

- Kill switch generates **sequenced** cancel messages — they appear in the journal and are deterministically replayable
- Kill switch also sets a **block flag** on the participant/instrument in the risk service, rejecting any new orders until manually cleared
- All kill switch activations are logged as risk events (EventType 401)

---

## Audit / Event Hooks

| Event | When | Payload |
|-------|------|---------|
| RiskReject | Order fails any risk check | ParticipantId, RejectCode, LimitValue, OrderValue |
| ThrottleReject | Session exceeds rate limit | SessionId, CurrentRate, Limit |
| KillSwitchActivation | Operator activates kill switch | Scope, TargetId, OperatorId |
| LimitUpdate | Risk limits changed at runtime | ParticipantId, OldLimits, NewLimits, OperatorId |

All events published to the surveillance export stream and audit trail.

---

## Stub Implementation (M1)

```cpp
namespace risk {

class StubRiskService : public IRiskService {
public:
    RiskResponse check(const RiskRequest& /* req */) override {
        return { RiskResult::PASS, {} };  // pass everything
    }

    uint32_t kill_switch(KillSwitchScope, uint32_t) override {
        return 0;  // no-op
    }

    void update_limits(engine::ParticipantId, const RiskLimits&) override {
        // no-op
    }
};

}  // namespace risk
```

Swap to real implementation after M1 by changing the factory/configuration.

---

## FR-1 vs Phase 2+ Scope

| Check | FR-1 | Phase 2+ |
|-------|:----:|:--------:|
| Fat-finger price | Yes | Yes |
| Max quantity | Yes | Yes |
| Max notional | Yes | Yes |
| Message rate throttle | Yes | Yes |
| Max open orders | Yes | Yes |
| Kill switch | Yes | Yes |
| Credit / exposure limits | No | Yes |
| Position limits | No | Yes |
| Short-sell controls | No | N/A (digital assets) |
| Cross-asset exposure | No | Phase 3 |
