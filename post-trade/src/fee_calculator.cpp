#include "posttrade/fee_calculator.h"

#include <cmath>
#include <cstdlib>
#include <limits>

namespace posttrade {

namespace {

// notional * fee_bps / 10000 computed in 128-bit so crafted price/qty cannot
// trigger signed overflow (UB). Result saturates to the int64_t range.
int64_t fee_from_notional(__int128 notional, int64_t fee_bps) {
    const __int128 fee = notional * fee_bps / 10000;
    constexpr __int128 kMax = std::numeric_limits<int64_t>::max();
    constexpr __int128 kMin = std::numeric_limits<int64_t>::min();
    if (fee > kMax) return std::numeric_limits<int64_t>::max();
    if (fee < kMin) return std::numeric_limits<int64_t>::min();
    return static_cast<int64_t>(fee);
}

std::pair<int64_t, int64_t> fees_for(engine::Price price, engine::Quantity qty,
                                     const FeeSchedule& sched) {
    __int128 p = price;
    if (p < 0) p = -p;  // safe in 128-bit even for INT64_MIN
    const __int128 notional = p * static_cast<__int128>(qty);
    return {fee_from_notional(notional, sched.maker_fee_bps),
            fee_from_notional(notional, sched.taker_fee_bps)};
}

}  // namespace

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
        return fees_for(price, quantity, sched);
    }

    const auto& sched = it->second;
    return fees_for(price, quantity, sched);
}

}  // namespace posttrade
