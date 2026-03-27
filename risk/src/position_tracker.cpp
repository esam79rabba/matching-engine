#include "risk/position_tracker.h"

namespace risk {

void PositionTracker::on_fill(engine::ParticipantId participant,
                               engine::InstrumentId instrument,
                               engine::Side side, engine::Quantity qty) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& pos = positions_[{participant, instrument}];
    if (side == engine::Side::Buy) {
        pos.net_qty += static_cast<int64_t>(qty);
        pos.buy_qty += qty;
    } else {
        pos.net_qty -= static_cast<int64_t>(qty);
        pos.sell_qty += qty;
    }
}

PositionTracker::Position PositionTracker::get_position(
    engine::ParticipantId participant, engine::InstrumentId instrument) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = positions_.find({participant, instrument});
    if (it != positions_.end()) {
        return it->second;
    }
    return {};
}

bool PositionTracker::check_limit(engine::ParticipantId participant,
                                   engine::InstrumentId instrument,
                                   engine::Side side, engine::Quantity qty,
                                   int64_t max_long, int64_t max_short) const {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t current_net = 0;
    auto it = positions_.find({participant, instrument});
    if (it != positions_.end()) {
        current_net = it->second.net_qty;
    }

    int64_t hypothetical_net = current_net;
    if (side == engine::Side::Buy) {
        hypothetical_net += static_cast<int64_t>(qty);
    } else {
        hypothetical_net -= static_cast<int64_t>(qty);
    }

    // Check long limit (positive position)
    if (hypothetical_net > 0 && hypothetical_net > max_long) {
        return false;
    }
    // Check short limit (negative position, compare absolute value)
    if (hypothetical_net < 0 && (-hypothetical_net) > max_short) {
        return false;
    }

    return true;
}

void PositionTracker::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    positions_.clear();
}

}  // namespace risk
