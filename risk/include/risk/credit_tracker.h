#pragma once
#include "engine/types.h"
#include <unordered_map>
#include <mutex>

namespace risk {

/// Tracks credit exposure per participant.
/// Exposure = sum of (price * qty) for all open orders + filled trades.
class CreditTracker {
public:
    /// Update exposure after a fill (add fill notional).
    void on_fill(engine::ParticipantId participant, engine::Price price, engine::Quantity qty);

    /// Add exposure for a new resting order (reserved credit).
    void on_order_accepted(engine::ParticipantId participant, engine::Price price, engine::Quantity qty);

    /// Release exposure when an order is cancelled.
    void on_order_cancelled(engine::ParticipantId participant, engine::Price price, engine::Quantity qty);

    /// Check if a proposed order would breach the credit limit.
    bool check_limit(engine::ParticipantId participant,
                     engine::Price price, engine::Quantity qty,
                     int64_t max_credit) const;

    /// Get current exposure for a participant.
    int64_t get_exposure(engine::ParticipantId participant) const;

    /// Set credit limit for a participant.
    void set_limit(engine::ParticipantId participant, int64_t limit);

    /// Reset all (for testing / EOD).
    void clear();

private:
    mutable std::mutex mutex_;
    std::unordered_map<engine::ParticipantId, int64_t> exposure_;  // Current exposure
    std::unordered_map<engine::ParticipantId, int64_t> limits_;    // Credit limits
};

}  // namespace risk
