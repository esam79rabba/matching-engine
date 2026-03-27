#pragma once

#include "engine/matching.h"
#include <string>
#include <vector>
#include <optional>

namespace config {

struct ParticipantConfig {
    engine::ParticipantId participant_id;
    std::string name;
    std::string sender_comp_id;
    uint32_t max_order_rate;
    double fat_finger_price_pct;
    double fat_finger_max_notional;
};

struct SessionScheduleEntry {
    std::string state;  // "PRE_OPEN", "CONTINUOUS", "CLOSED"
    std::string time;   // "HH:MM:SS"
};

struct SessionConfig {
    std::string name;
    std::vector<SessionScheduleEntry> transitions;
    std::string timezone;
    std::vector<ParticipantConfig> participants;
};

struct InstrumentEntry {
    engine::InstrumentId instrument_id;
    std::string symbol;
    std::string description;
    engine::InstrumentConfig config;
    engine::PartitionId partition_id;
};

struct LoadResult {
    bool success = false;
    std::string error;
    std::vector<InstrumentEntry> instruments;
    std::optional<SessionConfig> session;
};

class JsonConfigLoader {
public:
    /// Load instruments from a JSON file.
    static LoadResult load_instruments(const std::string& path);

    /// Load session/participant config from a JSON file.
    static LoadResult load_sessions(const std::string& path);
};

}  // namespace config
