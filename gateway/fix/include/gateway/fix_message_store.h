#pragma once

#include "engine/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <optional>

namespace gateway {

/// In-memory store of sent FIX messages for replay on ResendRequest.
/// Keyed by (sender_comp_id, outbound_seq_num).
///
/// This is a bounded in-memory store -- messages older than max_stored
/// are evicted. If a ResendRequest asks for evicted messages, a GapFill
/// is sent instead.
///
/// Production: back with persistent storage (file or DB).
/// Prototype: in-memory with configurable depth.
class FixMessageStore {
public:
    explicit FixMessageStore(uint32_t max_messages_per_session = 10000);

    /// Store a sent message for potential replay.
    void store(const std::string& sender_comp_id, uint64_t seq_num,
               const std::string& fix_message);

    /// Retrieve a stored message. Returns nullopt if evicted or not found.
    std::optional<std::string> get(const std::string& sender_comp_id,
                                    uint64_t seq_num) const;

    /// Retrieve a range of messages for replay.
    /// Returns pairs of (seq_num, message). Missing messages in the range
    /// are omitted (caller should gap-fill those).
    std::vector<std::pair<uint64_t, std::string>> get_range(
        const std::string& sender_comp_id,
        uint64_t begin_seq, uint64_t end_seq) const;

    /// Clear all stored messages for a session.
    void clear(const std::string& sender_comp_id);

    /// Get count of stored messages for a session.
    uint32_t count(const std::string& sender_comp_id) const;

private:
    uint32_t max_per_session_;
    mutable std::mutex mutex_;

    struct SessionMessages {
        // seq_num -> message
        std::unordered_map<uint64_t, std::string> messages;
        uint64_t oldest_seq = 0;
    };

    std::unordered_map<std::string, SessionMessages> sessions_;

    void evict_old(SessionMessages& sm);
};

}  // namespace gateway
