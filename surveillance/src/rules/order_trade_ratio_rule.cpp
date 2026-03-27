#include <surveillance/rules/order_trade_ratio_rule.h>

#include <algorithm>

namespace surveillance {

OrderToTradeRatioRule::OrderToTradeRatioRule(uint64_t window_ns, double threshold)
    : window_ns_(window_ns), threshold_(threshold) {}

std::string OrderToTradeRatioRule::name() const {
    return "OrderToTradeRatio";
}

void OrderToTradeRatioRule::prune_old_events(
    std::vector<TimestampedEvent>& events,
    engine::Timestamp now) const {
    auto cutoff = (now > window_ns_) ? now - window_ns_ : 0ULL;
    events.erase(
        std::remove_if(events.begin(), events.end(),
                        [cutoff](const TimestampedEvent& e) {
                            return e.timestamp < cutoff;
                        }),
        events.end());
}

void OrderToTradeRatioRule::on_order(engine::ParticipantId participant,
                                      engine::InstrumentId /*instrument*/,
                                      engine::Side /*side*/,
                                      engine::Price /*price*/,
                                      engine::Quantity /*quantity*/,
                                      engine::Timestamp timestamp) {
    auto& evts = events_[participant];
    prune_old_events(evts, timestamp);
    evts.push_back(TimestampedEvent{timestamp, false});
}

void OrderToTradeRatioRule::on_cancel(engine::ParticipantId /*participant*/,
                                       engine::InstrumentId /*instrument*/,
                                       engine::Timestamp /*timestamp*/) {
    // Order-to-trade ratio only counts orders and trades.
}

void OrderToTradeRatioRule::on_trade(engine::ParticipantId buyer,
                                      engine::ParticipantId seller,
                                      engine::InstrumentId /*instrument*/,
                                      engine::Price /*price*/,
                                      engine::Quantity /*quantity*/,
                                      engine::Timestamp timestamp) {
    // Record trade for both buyer and seller.
    for (auto pid : {buyer, seller}) {
        auto& evts = events_[pid];
        prune_old_events(evts, timestamp);
        evts.push_back(TimestampedEvent{timestamp, true});
    }
}

std::vector<Alert> OrderToTradeRatioRule::check() {
    for (auto& [participant, evts] : events_) {
        if (evts.empty()) continue;

        size_t orders = 0;
        size_t trades = 0;
        for (const auto& e : evts) {
            if (e.is_trade) ++trades;
            else ++orders;
        }

        // If no trades, and orders > threshold, alert.
        // If trades > 0, check ratio.
        double ratio = (trades == 0)
                        ? static_cast<double>(orders)
                        : static_cast<double>(orders) / static_cast<double>(trades);

        if (ratio > threshold_ && orders > 0) {
            pending_alerts_.push_back(Alert{
                .alert_id       = next_alert_id_++,
                .severity       = AlertSeverity::Medium,
                .rule_name      = name(),
                .participant_id = participant,
                .instrument_id  = 0,  // Aggregated across instruments
                .description    = "Excessive order-to-trade ratio: " +
                                  std::to_string(orders) + " orders / " +
                                  std::to_string(trades) + " trades = " +
                                  std::to_string(ratio),
                .timestamp      = evts.back().timestamp,
            });
            evts.clear();  // Reset after alert
        }
    }

    std::vector<Alert> result = std::move(pending_alerts_);
    pending_alerts_.clear();
    return result;
}

}  // namespace surveillance
