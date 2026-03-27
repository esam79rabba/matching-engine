#pragma once

#include <surveillance/rule.h>

#include <unordered_map>
#include <vector>

namespace surveillance {

/// Detects quote stuffing: excessive message rate per participant.
/// If > threshold (default 500) messages per second, generates High alert.
class QuoteStuffingRule : public ISurveillanceRule {
public:
    /// @param window_ns    Time window in nanoseconds (default: 1 second)
    /// @param threshold    Max messages per window (default: 500)
    explicit QuoteStuffingRule(uint64_t window_ns = 1'000'000'000ULL,
                               uint64_t threshold = 500);

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
    uint64_t window_ns_;
    uint64_t threshold_;
    std::unordered_map<engine::ParticipantId, std::vector<engine::Timestamp>> messages_;
    std::vector<Alert> pending_alerts_;
    uint64_t next_alert_id_{1};

    void prune_old_messages(std::vector<engine::Timestamp>& msgs,
                            engine::Timestamp now) const;
};

}  // namespace surveillance
