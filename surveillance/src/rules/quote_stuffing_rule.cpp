#include <surveillance/rules/quote_stuffing_rule.h>

#include <algorithm>

namespace surveillance {

QuoteStuffingRule::QuoteStuffingRule(uint64_t window_ns, uint64_t threshold)
    : window_ns_(window_ns), threshold_(threshold) {}

std::string QuoteStuffingRule::name() const {
    return "QuoteStuffing";
}

void QuoteStuffingRule::prune_old_messages(
    std::vector<engine::Timestamp>& msgs,
    engine::Timestamp now) const {
    auto cutoff = (now > window_ns_) ? now - window_ns_ : 0ULL;
    msgs.erase(
        std::remove_if(msgs.begin(), msgs.end(),
                        [cutoff](engine::Timestamp ts) {
                            return ts < cutoff;
                        }),
        msgs.end());
}

void QuoteStuffingRule::on_order(engine::ParticipantId participant,
                                  engine::InstrumentId /*instrument*/,
                                  engine::Side /*side*/,
                                  engine::Price /*price*/,
                                  engine::Quantity /*quantity*/,
                                  engine::Timestamp timestamp) {
    auto& msgs = messages_[participant];
    prune_old_messages(msgs, timestamp);
    msgs.push_back(timestamp);
}

void QuoteStuffingRule::on_cancel(engine::ParticipantId participant,
                                   engine::InstrumentId /*instrument*/,
                                   engine::Timestamp timestamp) {
    auto& msgs = messages_[participant];
    prune_old_messages(msgs, timestamp);
    msgs.push_back(timestamp);
}

void QuoteStuffingRule::on_trade(engine::ParticipantId /*buyer*/,
                                  engine::ParticipantId /*seller*/,
                                  engine::InstrumentId /*instrument*/,
                                  engine::Price /*price*/,
                                  engine::Quantity /*quantity*/,
                                  engine::Timestamp /*timestamp*/) {
    // Quote stuffing tracks message rate (orders + cancels), not trades.
}

std::vector<Alert> QuoteStuffingRule::check() {
    for (auto& [participant, msgs] : messages_) {
        if (msgs.size() > threshold_) {
            pending_alerts_.push_back(Alert{
                .alert_id       = next_alert_id_++,
                .severity       = AlertSeverity::High,
                .rule_name      = name(),
                .participant_id = participant,
                .instrument_id  = 0,  // Aggregated across instruments
                .description    = "Quote stuffing detected: " +
                                  std::to_string(msgs.size()) +
                                  " messages in window (threshold: " +
                                  std::to_string(threshold_) + ")",
                .timestamp      = msgs.back(),
            });
            msgs.clear();  // Reset after alert
        }
    }

    std::vector<Alert> result = std::move(pending_alerts_);
    pending_alerts_.clear();
    return result;
}

}  // namespace surveillance
