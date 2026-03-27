#pragma once

#include <string>
#include <vector>

#include <engine/types.h>
#include <surveillance/alert.h>

namespace surveillance {

class ISurveillanceRule {
public:
    virtual ~ISurveillanceRule() = default;

    virtual std::string name() const = 0;

    virtual void on_order(engine::ParticipantId participant,
                          engine::InstrumentId instrument,
                          engine::Side side,
                          engine::Price price,
                          engine::Quantity quantity,
                          engine::Timestamp timestamp) = 0;

    virtual void on_cancel(engine::ParticipantId participant,
                           engine::InstrumentId instrument,
                           engine::Timestamp timestamp) = 0;

    virtual void on_trade(engine::ParticipantId buyer,
                          engine::ParticipantId seller,
                          engine::InstrumentId instrument,
                          engine::Price price,
                          engine::Quantity quantity,
                          engine::Timestamp timestamp) = 0;

    /// Returns pending alerts and clears them.
    virtual std::vector<Alert> check() = 0;
};

}  // namespace surveillance
