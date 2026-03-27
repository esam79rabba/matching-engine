#include <surveillance/rules/layering_rule.h>

#include <algorithm>

namespace surveillance {

LayeringRule::LayeringRule(uint64_t window_ns, double threshold)
    : window_ns_(window_ns), threshold_(threshold) {}

std::string LayeringRule::name() const {
    return "Layering";
}

void LayeringRule::prune_old_events(std::vector<Event>& events,
                                     engine::Timestamp now) const {
    auto cutoff = (now > window_ns_) ? now - window_ns_ : 0ULL;
    events.erase(
        std::remove_if(events.begin(), events.end(),
                        [cutoff](const Event& e) {
                            return e.timestamp < cutoff;
                        }),
        events.end());
}

void LayeringRule::on_order(engine::ParticipantId participant,
                             engine::InstrumentId instrument,
                             engine::Side side,
                             engine::Price /*price*/,
                             engine::Quantity /*quantity*/,
                             engine::Timestamp timestamp) {
    ParticipantKey key{participant, instrument, side};
    auto& evts = events_[key];
    prune_old_events(evts, timestamp);
    evts.push_back(Event{timestamp, false});
}

void LayeringRule::on_cancel(engine::ParticipantId participant,
                              engine::InstrumentId instrument,
                              engine::Timestamp timestamp) {
    // Record cancel for both sides since we don't know which side was cancelled.
    // In practice, you'd track the side from the original order.
    // For simplicity, record cancel for both sides.
    for (auto side : {engine::Side::Buy, engine::Side::Sell}) {
        ParticipantKey key{participant, instrument, side};
        auto it = events_.find(key);
        if (it != events_.end()) {
            prune_old_events(it->second, timestamp);
            it->second.push_back(Event{timestamp, true});
        }
    }
}

void LayeringRule::on_trade(engine::ParticipantId /*buyer*/,
                             engine::ParticipantId /*seller*/,
                             engine::InstrumentId /*instrument*/,
                             engine::Price /*price*/,
                             engine::Quantity /*quantity*/,
                             engine::Timestamp /*timestamp*/) {
    // Layering rule focuses on order/cancel patterns, not trades.
}

std::vector<Alert> LayeringRule::check() {
    for (auto& [key, evts] : events_) {
        if (evts.empty()) continue;

        size_t orders = 0;
        size_t cancels = 0;
        for (const auto& e : evts) {
            if (e.is_cancel) ++cancels;
            else ++orders;
        }

        if (orders >= 5 && cancels > 0 &&
            static_cast<double>(cancels) / static_cast<double>(orders) > threshold_) {
            pending_alerts_.push_back(Alert{
                .alert_id       = next_alert_id_++,
                .severity       = AlertSeverity::High,
                .rule_name      = name(),
                .participant_id = key.participant,
                .instrument_id  = key.instrument,
                .description    = "Layering/spoofing detected: " +
                                  std::to_string(cancels) + " cancels / " +
                                  std::to_string(orders) + " orders exceeds threshold",
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
