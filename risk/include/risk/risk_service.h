#pragma once

#include "engine/types.h"
#include "risk/rate_limiter.h"
#include "risk/position_tracker.h"
#include "risk/credit_tracker.h"
#include <chrono>
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
    int64_t          max_long_position = 0;          // Max long position per instrument (0 = disabled)
    int64_t          max_short_position = 0;         // Max short position per instrument (0 = disabled)
    int64_t          max_credit        = 0;          // Max credit exposure (0 = disabled)
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
    uint16_t              flags = 0;         // Order flags (FLAG_REDUCE_ONLY, etc.)
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
/// Latency budget: < 5 us (Profile A engineering target).
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
///   - Open order count tracking per participant
///   - Position limits (per-instrument long/short caps)
///   - Credit / exposure limits (net debit tracking)
class BasicRiskService : public IRiskService {
public:
    BasicRiskService();
    BasicRiskService(uint32_t max_messages_per_second, uint32_t window_ms = 1000);

    RiskResponse check(const RiskRequest& req) override;

    void update_limits(engine::ParticipantId participant_id,
                       const RiskLimits& limits) override;

    void block_participant(engine::ParticipantId participant_id) override;
    void unblock_participant(engine::ParticipantId participant_id) override;
    [[nodiscard]] bool is_blocked(engine::ParticipantId participant_id) const override;

    /// Set default limits for participants without explicit configuration.
    void set_default_limits(const RiskLimits& limits) { default_limits_ = limits; }

    // ─── Open order count tracking ───────────────────────────

    /// Called after a new order is successfully inserted (resting) in the book.
    void on_order_inserted(engine::ParticipantId pid);

    /// Called after an order is removed from the book (cancel, fill, or expire).
    void on_order_removed(engine::ParticipantId pid);

    /// Query current open order count for a participant.
    [[nodiscard]] uint32_t open_order_count(engine::ParticipantId pid) const;

    /// Check if adding an order would exceed the participant's open order limit.
    /// Returns true if the order should be rejected.
    [[nodiscard]] bool would_exceed_open_order_limit(engine::ParticipantId pid) const;

    /// Access the position tracker.
    [[nodiscard]] PositionTracker& position_tracker() noexcept { return position_tracker_; }
    [[nodiscard]] const PositionTracker& position_tracker() const noexcept { return position_tracker_; }

    /// Access the credit tracker.
    [[nodiscard]] CreditTracker& credit_tracker() noexcept { return credit_tracker_; }
    [[nodiscard]] const CreditTracker& credit_tracker() const noexcept { return credit_tracker_; }

private:
    RiskLimits default_limits_;
    std::unordered_map<engine::ParticipantId, RiskLimits> participant_limits_;
    std::unordered_set<engine::ParticipantId> blocked_;
    std::unordered_map<engine::ParticipantId, uint32_t> open_order_counts_;
    uint32_t default_max_messages_per_second_ = 0;
    RateLimiter rate_limiter_;
    PositionTracker position_tracker_;
    CreditTracker credit_tracker_;

    [[nodiscard]] const RiskLimits& get_limits(engine::ParticipantId id) const;
};

}  // namespace risk
