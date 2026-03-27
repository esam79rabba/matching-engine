#pragma once

#include "engine/types.h"
#include <cstdint>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace risk {

/// Risk limits per participant (or per participant + instrument group).
/// These are loaded from reference data and updatable at runtime.
struct RiskLimits {
    engine::Quantity max_order_qty    = 1000000;   // Max quantity per order
    double           max_notional     = 10000000.0; // Max notional per order (in quote currency)
    double           max_price_deviation_pct = 20.0; // Max deviation from reference price (%)
    uint32_t         max_message_rate  = 1000;       // Max messages per second (placeholder)
    uint32_t         max_open_orders   = 10000;      // Max resting orders (placeholder)
};

/// Request submitted to the risk service before sequencing.
/// Positioned between gateway and sequencer in the pipeline.
struct RiskRequest {
    engine::ClientOrderId client_order_id;
    engine::InstrumentId  instrument_id;
    engine::ParticipantId participant_id;
    engine::AccountId     account_id;
    engine::SessionId     session_id;
    engine::Side          side;
    engine::OrderType     order_type;
    engine::Price         price;             // Integer-encoded
    engine::Quantity      quantity;
    engine::Price         reference_price;   // Current reference (from book or ref data)
    engine::Price         price_multiplier;  // For notional calculation
};

enum class RiskResult : uint8_t {
    PASS   = 0,
    REJECT = 1,
};

struct RiskResponse {
    RiskResult          result;
    engine::RejectCode  reject_code;
};

/// Abstract risk service interface.
///
/// Positioned between the gateway adapter and the partition sequencer.
/// The gateway calls check() synchronously before submitting to the engine.
///
/// Latency budget: < 5 µs (Profile A engineering target).
/// Single-threaded per gateway (no cross-thread synchronization needed).
///
/// Generic — no asset-class-specific logic. All thresholds from RiskLimits.
class IRiskService {
public:
    virtual ~IRiskService() = default;

    virtual RiskResponse check(const RiskRequest& req) = 0;

    /// Update limits at runtime (no restart). Thread-safe not required
    /// if called from the same thread as check().
    virtual void update_limits(engine::ParticipantId participant_id,
                               const RiskLimits& limits) = 0;

    /// Kill switch: mark participant as blocked. All subsequent checks
    /// return REJECT until unblocked.
    virtual void block_participant(engine::ParticipantId participant_id) = 0;
    virtual void unblock_participant(engine::ParticipantId participant_id) = 0;
    [[nodiscard]] virtual bool is_blocked(engine::ParticipantId participant_id) const = 0;
};


/// FR-1 risk service with real fat-finger checks.
///
/// Implements:
///   - Max quantity per order
///   - Max notional per order
///   - Price deviation from reference price
///   - Kill switch (block/unblock participant)
///
/// Not yet implemented (Phase 2):
///   - Per-session message rate throttling
///   - Max open orders tracking
///   - Credit / exposure limits
///   - Position limits
class BasicRiskService : public IRiskService {
public:
    BasicRiskService();

    RiskResponse check(const RiskRequest& req) override;

    void update_limits(engine::ParticipantId participant_id,
                       const RiskLimits& limits) override;

    void block_participant(engine::ParticipantId participant_id) override;
    void unblock_participant(engine::ParticipantId participant_id) override;
    [[nodiscard]] bool is_blocked(engine::ParticipantId participant_id) const override;

    /// Set default limits for participants without explicit configuration.
    void set_default_limits(const RiskLimits& limits) { default_limits_ = limits; }

private:
    RiskLimits default_limits_;
    std::unordered_map<engine::ParticipantId, RiskLimits> participant_limits_;
    std::unordered_set<engine::ParticipantId> blocked_;

    [[nodiscard]] const RiskLimits& get_limits(engine::ParticipantId id) const;
};

}  // namespace risk
