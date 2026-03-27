#include "posttrade/fee_calculator.h"

#include <cmath>
#include <cstdlib>

namespace posttrade {

void FeeCalculator::set_schedule(engine::InstrumentId iid,
                                 const FeeSchedule& schedule) {
    std::lock_guard<std::mutex> lock(mtx_);
    schedules_[iid] = schedule;
}

void FeeCalculator::set_default_schedule(const FeeSchedule& schedule) {
    std::lock_guard<std::mutex> lock(mtx_);
    default_schedule_ = schedule;
}

std::pair<int64_t, int64_t> FeeCalculator::calculate(
    engine::InstrumentId iid, engine::Price price,
    engine::Quantity quantity, [[maybe_unused]] engine::Side aggressor_side) const {
    std::lock_guard<std::mutex> lock(mtx_);

    // Look up instrument-specific schedule, then default, else zero.
    auto it = schedules_.find(iid);
    if (it == schedules_.end()) {
        if (!default_schedule_.has_value()) {
            return {0, 0};
        }
        const auto& sched = default_schedule_.value();
        int64_t notional   = std::abs(price) * static_cast<int64_t>(quantity);
        int64_t maker_fee  = notional * sched.maker_fee_bps / 10000;
        int64_t taker_fee  = notional * sched.taker_fee_bps / 10000;
        return {maker_fee, taker_fee};
    }

    const auto& sched = it->second;
    int64_t notional   = std::abs(price) * static_cast<int64_t>(quantity);
    int64_t maker_fee  = notional * sched.maker_fee_bps / 10000;
    int64_t taker_fee  = notional * sched.taker_fee_bps / 10000;
    return {maker_fee, taker_fee};
}

}  // namespace posttrade
