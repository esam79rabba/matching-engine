#include <surveillance/surveillance_engine.h>

namespace surveillance {

void SurveillanceEngine::add_rule(std::unique_ptr<ISurveillanceRule> rule) {
    rules_.push_back(std::move(rule));
}

void SurveillanceEngine::on_order(engine::ParticipantId participant,
                                   engine::InstrumentId instrument,
                                   engine::Side side,
                                   engine::Price price,
                                   engine::Quantity quantity,
                                   engine::Timestamp timestamp) {
    for (auto& rule : rules_) {
        rule->on_order(participant, instrument, side, price, quantity, timestamp);
    }
}

void SurveillanceEngine::on_cancel(engine::ParticipantId participant,
                                    engine::InstrumentId instrument,
                                    engine::Timestamp timestamp) {
    for (auto& rule : rules_) {
        rule->on_cancel(participant, instrument, timestamp);
    }
}

void SurveillanceEngine::on_trade(engine::ParticipantId buyer,
                                   engine::ParticipantId seller,
                                   engine::InstrumentId instrument,
                                   engine::Price price,
                                   engine::Quantity quantity,
                                   engine::Timestamp timestamp) {
    for (auto& rule : rules_) {
        rule->on_trade(buyer, seller, instrument, price, quantity, timestamp);
    }
}

void SurveillanceEngine::drain_rules_into_buffer() {
    for (auto& rule : rules_) {
        auto alerts = rule->check();
        buffered_alerts_.insert(buffered_alerts_.end(),
                                std::make_move_iterator(alerts.begin()),
                                std::make_move_iterator(alerts.end()));
    }
}

std::vector<Alert> SurveillanceEngine::collect_alerts() {
    drain_rules_into_buffer();
    std::vector<Alert> result = std::move(buffered_alerts_);
    buffered_alerts_.clear();
    return result;
}

size_t SurveillanceEngine::rule_count() const {
    return rules_.size();
}

size_t SurveillanceEngine::alert_count() {
    drain_rules_into_buffer();
    return buffered_alerts_.size();
}

}  // namespace surveillance
