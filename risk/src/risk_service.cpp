#include "risk/risk_service.h"
#include <unordered_map>
#include <unordered_set>

namespace risk {

BasicRiskService::BasicRiskService() = default;

RiskResponse BasicRiskService::check(const RiskRequest& req) {
    // ── Kill switch ─────────────────────────────────────────
    if (is_blocked(req.participant_id)) {
        return { RiskResult::REJECT, engine::RejectCode::RiskCheckFailed };
    }

    const auto& limits = get_limits(req.participant_id);

    // ── Max quantity ────────────────────────────────────────
    if (req.quantity > limits.max_order_qty) {
        return { RiskResult::REJECT, engine::RejectCode::QuantityExceedsMax };
    }

    // ── Max notional ────────────────────────────────────────
    if (req.price_multiplier > 0 && req.price > 0) {
        double notional = static_cast<double>(req.price) *
                          static_cast<double>(req.quantity) /
                          static_cast<double>(req.price_multiplier);
        if (notional > limits.max_notional) {
            return { RiskResult::REJECT, engine::RejectCode::RiskCheckFailed };
        }
    }

    // ── Price deviation (limit orders only) ─────────────────
    if (req.order_type == engine::OrderType::Limit &&
        req.reference_price > 0 && limits.max_price_deviation_pct > 0.0) {
        double deviation = std::abs(
            static_cast<double>(req.price - req.reference_price)) /
            static_cast<double>(req.reference_price) * 100.0;
        if (deviation > limits.max_price_deviation_pct) {
            return { RiskResult::REJECT, engine::RejectCode::PriceOutsideBands };
        }
    }

    // ── Placeholder: message rate (not enforced in FR-1 M1) ─
    // TODO: Implement sliding-window or token-bucket rate limiter.

    // ── Max open orders ─────────────────────────────────────
    if (would_exceed_open_order_limit(req.participant_id)) {
        return { RiskResult::REJECT, engine::RejectCode::RiskCheckFailed };
    }

    return { RiskResult::PASS, engine::RejectCode::None };
}

void BasicRiskService::update_limits(engine::ParticipantId participant_id,
                                      const RiskLimits& limits) {
    participant_limits_[participant_id] = limits;
}

void BasicRiskService::block_participant(engine::ParticipantId participant_id) {
    blocked_.insert(participant_id);
}

void BasicRiskService::unblock_participant(engine::ParticipantId participant_id) {
    blocked_.erase(participant_id);
}

bool BasicRiskService::is_blocked(engine::ParticipantId participant_id) const {
    return blocked_.count(participant_id) > 0;
}

const RiskLimits& BasicRiskService::get_limits(engine::ParticipantId id) const {
    auto it = participant_limits_.find(id);
    return (it != participant_limits_.end()) ? it->second : default_limits_;
}

// ═══════════════════════════════════════════════════════════════
// Open Order Count Tracking
// ═══════════════════════════════════════════════════════════════

void BasicRiskService::on_order_inserted(engine::ParticipantId pid) {
    ++open_order_counts_[pid];
}

void BasicRiskService::on_order_removed(engine::ParticipantId pid) {
    auto it = open_order_counts_.find(pid);
    if (it != open_order_counts_.end() && it->second > 0) {
        --it->second;
    }
}

uint32_t BasicRiskService::open_order_count(engine::ParticipantId pid) const {
    auto it = open_order_counts_.find(pid);
    return (it != open_order_counts_.end()) ? it->second : 0;
}

bool BasicRiskService::would_exceed_open_order_limit(engine::ParticipantId pid) const {
    const auto& limits = get_limits(pid);
    uint32_t current = open_order_count(pid);
    return current >= limits.max_open_orders;
}

}  // namespace risk
