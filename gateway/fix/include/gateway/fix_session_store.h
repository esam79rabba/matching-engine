#pragma once
#include "engine/types.h"
#include <string>
#include <unordered_map>
#include <optional>

namespace gateway {

/// Persisted session state — survives restarts.
struct PersistedSessionState {
    engine::SessionId     session_id;
    std::string           sender_comp_id;
    std::string           target_comp_id;
    uint64_t              inbound_seq = 1;
    uint64_t              outbound_seq = 1;
};

/// File-based FIX session state store.
/// Format: simple text file, one session per line:
///   session_id|sender_comp_id|target_comp_id|inbound_seq|outbound_seq
///
/// Production: replace with a proper database or embedded KV store.
class FixSessionStore {
public:
    explicit FixSessionStore(const std::string& path);

    /// Load all sessions from file. Returns false if file doesn't exist (not an error — fresh start).
    bool load();

    /// Save all sessions to file. Returns false on I/O error.
    bool save() const;

    /// Get persisted state for a session by sender_comp_id.
    std::optional<PersistedSessionState> get(const std::string& sender_comp_id) const;

    /// Update or insert session state.
    void upsert(const PersistedSessionState& state);

    /// Remove a session.
    void remove(const std::string& sender_comp_id);

    /// Get all sessions.
    const std::unordered_map<std::string, PersistedSessionState>& sessions() const { return sessions_; }

    /// Get file path.
    const std::string& path() const { return path_; }

private:
    std::string path_;
    std::unordered_map<std::string, PersistedSessionState> sessions_;  // keyed by sender_comp_id
};

}  // namespace gateway
