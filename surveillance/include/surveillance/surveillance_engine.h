#pragma once

#include <memory>
#include <vector>

#include <engine/types.h>
#include <surveillance/alert.h>
#include <surveillance/rule.h>

namespace surveillance {

class SurveillanceEngine {
public:
    void add_rule(std::unique_ptr<ISurveillanceRule> rule);

    void on_order(engine::ParticipantId participant,
                  engine::InstrumentId instrument,
                  engine::Side side,
                  engine::Price price,
                  engine::Quantity quantity,
                  engine::Timestamp timestamp);

    void on_cancel(engine::ParticipantId participant,
                   engine::InstrumentId instrument,
                   engine::Timestamp timestamp);

    void on_trade(engine::ParticipantId buyer,
                  engine::ParticipantId seller,
                  engine::InstrumentId instrument,
                  engine::Price price,
                  engine::Quantity quantity,
                  engine::Timestamp timestamp);

    /// Collects all pending alerts from all rules and returns them.
    std::vector<Alert> collect_alerts();

    size_t rule_count() const;

    /// Returns total uncollected alert count (drains rules into internal buffer).
    size_t alert_count();

private:
    std::vector<std::unique_ptr<ISurveillanceRule>> rules_;
    std::vector<Alert> buffered_alerts_;

    void drain_rules_into_buffer();
};

}  // namespace surveillance
