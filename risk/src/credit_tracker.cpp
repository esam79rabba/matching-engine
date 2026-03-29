#include "risk/credit_tracker.h"
#include <algorithm>

namespace risk {

void CreditTracker::on_fill(engine::ParticipantId participant,
                             engine::Price price, engine::Quantity qty) {
    std::lock_guard<std::mutex> lock(mutex_);
    exposure_[participant] += price * static_cast<int64_t>(qty);
}

void CreditTracker::on_order_accepted(engine::ParticipantId participant,
                                       engine::Price price, engine::Quantity qty) {
    std::lock_guard<std::mutex> lock(mutex_);
    exposure_[participant] += price * static_cast<int64_t>(qty);
}

void CreditTracker::on_order_cancelled(engine::ParticipantId participant,
                                        engine::Price price, engine::Quantity qty) {
    std::lock_guard<std::mutex> lock(mutex_);
    exposure_[participant] -= price * static_cast<int64_t>(qty);
    // Clamp to zero to avoid negative exposure from accounting mismatches
    if (exposure_[participant] < 0) {
        exposure_[participant] = 0;
    }
}

bool CreditTracker::check_limit(engine::ParticipantId participant,
                                 engine::Price price, engine::Quantity qty,
                                 int64_t max_credit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t current = 0;
    auto it = exposure_.find(participant);
    if (it != exposure_.end()) {
        current = it->second;
    }
    int64_t proposed_notional = price * static_cast<int64_t>(qty);
    return (current + proposed_notional) <= max_credit;
}

int64_t CreditTracker::get_exposure(engine::ParticipantId participant) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = exposure_.find(participant);
    return (it != exposure_.end()) ? it->second : 0;
}

void CreditTracker::set_limit(engine::ParticipantId participant, int64_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    limits_[participant] = limit;
}

int64_t CreditTracker::get_limit(engine::ParticipantId participant) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = limits_.find(participant);
    return (it != limits_.end()) ? it->second : 0;
}

void CreditTracker::reset_exposure(engine::ParticipantId participant) {
    std::lock_guard<std::mutex> lock(mutex_);
    exposure_[participant] = 0;
}

void CreditTracker::adjust_exposure(engine::ParticipantId participant, int64_t delta) {
    std::lock_guard<std::mutex> lock(mutex_);
    exposure_[participant] += delta;
    if (exposure_[participant] < 0) {
        exposure_[participant] = 0;
    }
}

void CreditTracker::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    exposure_.clear();
    limits_.clear();
}

}  // namespace risk
