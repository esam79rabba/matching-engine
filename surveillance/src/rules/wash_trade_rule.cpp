#include <surveillance/rules/wash_trade_rule.h>

namespace surveillance {

std::string WashTradeRule::name() const {
    return "WashTrade";
}

void WashTradeRule::on_order(engine::ParticipantId /*participant*/,
                              engine::InstrumentId /*instrument*/,
                              engine::Side /*side*/,
                              engine::Price /*price*/,
                              engine::Quantity /*quantity*/,
                              engine::Timestamp /*timestamp*/) {
    // Wash trade detection only looks at trades.
}

void WashTradeRule::on_cancel(engine::ParticipantId /*participant*/,
                               engine::InstrumentId /*instrument*/,
                               engine::Timestamp /*timestamp*/) {
    // Wash trade detection only looks at trades.
}

void WashTradeRule::on_trade(engine::ParticipantId buyer,
                              engine::ParticipantId seller,
                              engine::InstrumentId instrument,
                              engine::Price price,
                              engine::Quantity quantity,
                              engine::Timestamp timestamp) {
    if (buyer == seller) {
        pending_alerts_.push_back(Alert{
            .alert_id       = next_alert_id_++,
            .severity       = AlertSeverity::Critical,
            .rule_name      = name(),
            .participant_id = buyer,
            .instrument_id  = instrument,
            .description    = "Wash trade detected: participant " +
                              std::to_string(buyer) +
                              " is both buyer and seller, price=" +
                              std::to_string(price) +
                              " qty=" + std::to_string(quantity),
            .timestamp      = timestamp,
        });
    }
}

std::vector<Alert> WashTradeRule::check() {
    std::vector<Alert> result = std::move(pending_alerts_);
    pending_alerts_.clear();
    return result;
}

}  // namespace surveillance
