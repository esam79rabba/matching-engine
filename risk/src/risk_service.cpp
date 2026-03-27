#include "risk/risk_service.h"
#include <climits>
#include <unordered_map>
#include <unordered_set>

namespace risk {

BasicRiskService::BasicRiskService() = default;

BasicRiskService::BasicRiskService(uint32_t max_messages_per_second, uint32_t window_ms)
    : default_max_messages_per_second_(max_messages_per_second) {
    if (max_messages_per_second > 0) {
        RateLimiter::Config cfg;
        cfg.max_messages_per_window = max_messages_per_second;
        cfg.window_ms = window_ms;
        rate_limiter_.set_defaults(cfg);
    }
}

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
    {
        // Use order price for limits, reference price for market orders
        engine::Price check_price = (req.price > 0) ? req.price : req.reference_price;
        if (check_price > 0) {
            double divisor = (req.price_multiplier > 0) ? static_cast<double>(req.price_multiplier) : 1.0;
            double notional = static_cast<double>(check_price) *
                              static_cast<double>(req.quantity) / divisor;
            if (notional > limits.max_notional) {
                return { RiskResult::REJECT, engine::RejectCode::RiskCheckFailed };
            }
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

    // ── Message rate check ──────────────────────────────────
    if (default_max_messages_per_second_ > 0) {
        auto now = std::chrono::steady_clock::now();
        auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        if (!rate_limiter_.check_and_increment(req.session_id, static_cast<uint64_t>(ts))) {
            return { RiskResult::REJECT, engine::RejectCode::RateLimitExceeded };
        }
    }

    // ── Max open orders ─────────────────────────────────────
    if (would_exceed_open_order_limit(req.participant_id)) {
        return { RiskResult::REJECT, engine::RejectCode::RiskCheckFailed };
    }

    // ── Position limits ───────────────────────────────────────
    if (limits.max_long_position > 0 || limits.max_short_position > 0) {
        int64_t max_long = (limits.max_long_position > 0) ? limits.max_long_position : INT64_MAX;
        int64_t max_short = (limits.max_short_position > 0) ? limits.max_short_position : INT64_MAX;
        if (!position_tracker_.check_limit(req.participant_id, req.instrument_id,
                                           req.side, req.quantity,
                                           max_long, max_short)) {
            return { RiskResult::REJECT, engine::RejectCode::PositionLimitBreached };
        }
    }

    // ── Credit / exposure limits ──────────────────────────────
    if (limits.max_credit > 0) {
        if (!credit_tracker_.check_limit(req.participant_id, req.price, req.quantity,
                                         limits.max_credit)) {
            return { RiskResult::REJECT, engine::RejectCode::CreditLimitBreached };
        }
    }

    // ── Reduce-only check ──────────────────────────────────
    if ((req.flags & engine::FLAG_REDUCE_ONLY) != 0) {
        auto pos = position_tracker_.get_position(req.participant_id, req.instrument_id);
        // Buy reduce-only: only valid when short (net_qty < 0)
        // Sell reduce-only: only valid when long (net_qty > 0)
        bool would_increase = (req.side == engine::Side::Buy && pos.net_qty >= 0) ||
                               (req.side == engine::Side::Sell && pos.net_qty <= 0);
        if (would_increase) {
            return { RiskResult::REJECT, engine::RejectCode::ReduceOnlyWouldIncrease };
        }
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
