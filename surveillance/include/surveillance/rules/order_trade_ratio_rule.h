#pragma once

#include <surveillance/rule.h>

#include <unordered_map>
#include <vector>

namespace surveillance {

/// Tracks orders submitted vs trades executed per participant.
/// If ratio > threshold (default 100:1) in a window, generates Medium alert.
class OrderToTradeRatioRule : public ISurveillanceRule {
public:
    /// @param window_ns  Time window in nanoseconds (default: 60 seconds)
    /// @param threshold  Order-to-trade ratio threshold (default: 100.0)
    explicit OrderToTradeRatioRule(uint64_t window_ns = 60'000'000'000ULL,
                                   double threshold = 100.0);

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
    struct TimestampedEvent {
        engine::Timestamp timestamp;
        bool is_trade;
    };

    uint64_t window_ns_;
    double threshold_;
    std::unordered_map<engine::ParticipantId, std::vector<TimestampedEvent>> events_;
    std::vector<Alert> pending_alerts_;
    uint64_t next_alert_id_{1};

    void prune_old_events(std::vector<TimestampedEvent>& events,
                          engine::Timestamp now) const;
};

}  // namespace surveillance
