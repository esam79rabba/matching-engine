#pragma once

#include <cstdint>
#include <string>

#include <engine/types.h>

namespace surveillance {

enum class AlertSeverity : uint8_t {
    Low      = 1,
    Medium   = 2,
    High     = 3,
    Critical = 4
};

struct Alert {
    uint64_t             alert_id;
    AlertSeverity        severity;
    std::string          rule_name;
    engine::ParticipantId participant_id;
    engine::InstrumentId  instrument_id;
    std::string          description;
    engine::Timestamp    timestamp;
};

}  // namespace surveillance
