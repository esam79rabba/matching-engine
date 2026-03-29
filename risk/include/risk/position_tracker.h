#pragma once
#include "engine/types.h"
#include <unordered_map>
#include <mutex>
#include <vector>
#include <tuple>

namespace risk {

/// Tracks net position per participant per instrument.
/// Updated on every fill. Thread-safe for cross-partition queries.
class PositionTracker {
public:
    /// Key: (participant_id, instrument_id)
    using PosKey = std::pair<engine::ParticipantId, engine::InstrumentId>;

    struct Position {
        int64_t net_qty = 0;       // Positive = long, negative = short
        engine::Quantity buy_qty = 0;   // Total bought
        engine::Quantity sell_qty = 0;  // Total sold
    };

    /// Update position after a fill.
    void on_fill(engine::ParticipantId participant, engine::InstrumentId instrument,
                 engine::Side side, engine::Quantity qty);

    /// Get current position.
    Position get_position(engine::ParticipantId participant, engine::InstrumentId instrument) const;

    /// Check if a proposed order would breach limits.
    /// Returns true if the order would be OK, false if it breaches.
    bool check_limit(engine::ParticipantId participant, engine::InstrumentId instrument,
                     engine::Side side, engine::Quantity qty,
                     int64_t max_long, int64_t max_short) const;

    /// Get all positions for a participant.
    std::vector<std::pair<engine::InstrumentId, Position>> get_positions_for_participant(
        engine::ParticipantId participant) const;

    /// Reset all positions for a participant.
    void reset_participant(engine::ParticipantId participant);

    /// Get all positions (all participants, all instruments).
    std::vector<std::tuple<engine::ParticipantId, engine::InstrumentId, Position>> get_all_positions() const;

    /// Reset all positions (for testing / EOD).
    void clear();

private:
    struct PairHash {
        size_t operator()(const PosKey& k) const {
            return std::hash<uint64_t>()(static_cast<uint64_t>(k.first) << 32 | k.second);
        }
    };
    mutable std::mutex mutex_;
    std::unordered_map<PosKey, Position, PairHash> positions_;
};

}  // namespace risk
