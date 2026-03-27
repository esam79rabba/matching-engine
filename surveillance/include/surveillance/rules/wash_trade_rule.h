#pragma once

#include <surveillance/rule.h>

#include <atomic>
#include <vector>

namespace surveillance {

/// Detects when the same participant is both buyer and seller on a trade.
/// Generates an immediate Critical alert.
class WashTradeRule : public ISurveillanceRule {
public:
    std::string name() const override;

    void on_order(engine::ParticipantId participant,
                  engine::InstrumentId instrument,
                  engine::Side side,
                  engine::Price price,
                  engine::Quantity quantity,
                  engine::Timestamp timestamp) override;

    void on_cancel(engine::ParticipantId participant,
                   engine::InstrumentId instrument,
                   engine::Timestamp timestamp) override;

    void on_trade(engine::ParticipantId buyer,
                  engine::ParticipantId seller,
                  engine::InstrumentId instrument,
                  engine::Price price,
                  engine::Quantity quantity,
                  engine::Timestamp timestamp) override;

    std::vector<Alert> check() override;

private:
    std::vector<Alert> pending_alerts_;
    uint64_t next_alert_id_{1};
};

}  // namespace surveillance
