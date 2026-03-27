#pragma once

#include <engine/types.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

namespace posttrade {

struct FeeSchedule {
    int64_t maker_fee_bps = 10;  // 0.10% = 10 basis points
    int64_t taker_fee_bps = 20;  // 0.20% = 20 basis points
};

class FeeCalculator {
public:
    void set_schedule(engine::InstrumentId iid, const FeeSchedule& schedule);
    void set_default_schedule(const FeeSchedule& schedule);

    /// Calculate fees for a trade.
    /// Returns {maker_fee, taker_fee} in absolute terms (price * qty * bps / 10000).
    [[nodiscard]] std::pair<int64_t, int64_t> calculate(
        engine::InstrumentId iid, engine::Price price,
        engine::Quantity quantity, engine::Side aggressor_side) const;

private:
    mutable std::mutex mtx_;
    std::optional<FeeSchedule> default_schedule_;
    std::unordered_map<engine::InstrumentId, FeeSchedule> schedules_;
};

}  // namespace posttrade
