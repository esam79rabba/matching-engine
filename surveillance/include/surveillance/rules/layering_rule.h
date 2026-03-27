#pragma once

#include <surveillance/rule.h>

#include <unordered_map>
#include <vector>

namespace surveillance {

/// Detects layering/spoofing: high cancel ratio within a time window.
/// If cancel ratio > threshold (default 80%) in a window (default 5 seconds),
/// generates a High severity alert.
class LayeringRule : public ISurveillanceRule {
public:
    /// @param window_ns   Time window in nanoseconds (default: 5 seconds)
    /// @param threshold   Cancel ratio threshold (default: 0.80)
    explicit LayeringRule(uint64_t window_ns = 5'000'000'000ULL,
                          double threshold = 0.80);

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
    struct ParticipantKey {
        engine::ParticipantId participant;
        engine::InstrumentId instrument;
        engine::Side side;

        bool operator==(const ParticipantKey& o) const {
            return participant == o.participant &&
                   instrument == o.instrument &&
                   side == o.side;
        }
    };

    struct ParticipantKeyHash {
        size_t operator()(const ParticipantKey& k) const {
            auto h1 = std::hash<uint32_t>{}(k.participant);
            auto h2 = std::hash<uint32_t>{}(k.instrument);
            auto h3 = std::hash<uint8_t>{}(static_cast<uint8_t>(k.side));
            return h1 ^ (h2 << 16) ^ (h3 << 32);
        }
    };

    struct Event {
        engine::Timestamp timestamp;
        bool is_cancel;
    };

    uint64_t window_ns_;
    double threshold_;
    std::unordered_map<ParticipantKey, std::vector<Event>, ParticipantKeyHash> events_;
    std::vector<Alert> pending_alerts_;
    uint64_t next_alert_id_{1};

    void prune_old_events(std::vector<Event>& events, engine::Timestamp now) const;
};

}  // namespace surveillance
