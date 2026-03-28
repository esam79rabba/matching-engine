#pragma once
#include "engine/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <optional>

namespace ops {

struct SessionConfig {
    std::string sender_comp_id;      // Participant's CompID
    std::string target_comp_id;      // Exchange CompID (default "EXCHANGE")
    std::string fix_version;         // "FIX.4.2", "FIX.4.4", "FIXT.1.1" etc.
    uint32_t heartbeat_interval = 30;
    bool enabled = true;
    engine::ParticipantId participant_id = 0;
    std::string description;         // Human-readable label
    // Runtime state (not persisted in config, populated from live sessions)
    bool connected = false;
    uint64_t inbound_seq = 0;
    uint64_t outbound_seq = 0;
    std::string last_logon_time;
    std::string last_logout_time;
};

/// Persistent store for FIX session configurations.
/// Saves to/loads from a JSON file.
class SessionConfigStore {
public:
    explicit SessionConfigStore(const std::string& config_path);

    // CRUD
    void add_session(const SessionConfig& config);
    bool update_session(const std::string& sender_comp_id, const SessionConfig& config);
    bool remove_session(const std::string& sender_comp_id);
    std::vector<SessionConfig> list_sessions() const;
    std::optional<SessionConfig> get_session(const std::string& sender_comp_id) const;

    // Enable/disable
    bool enable_session(const std::string& sender_comp_id);
    bool disable_session(const std::string& sender_comp_id);

    // Sequence reset
    bool reset_sequences(const std::string& sender_comp_id);

    // Persistence
    void save() const;
    void load();

    size_t count() const;

private:
    std::string config_path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SessionConfig> sessions_;  // keyed by sender_comp_id
};

}  // namespace ops
