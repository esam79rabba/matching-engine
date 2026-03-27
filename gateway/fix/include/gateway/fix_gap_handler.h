#pragma once

#include "engine/types.h"
#include <string>
#include <vector>
#include <functional>
#include <optional>

namespace gateway {

/// First bounded FIX gap recovery implementation.
/// Handles basic sequence gap detection and ResendRequest/SequenceReset.
///
/// Scope for this milestone:
///   - Detect inbound sequence gap (received seq > expected seq)
///   - Generate ResendRequest (35=2) for the gap range
///   - Handle inbound SequenceReset-GapFill (35=4, 123=Y)
///   - Handle inbound SequenceReset-Reset (35=4, 123=N)
///   - Generate outbound SequenceReset-GapFill for requested ranges
///
/// NOT in scope (Phase 2+):
///   - PossDupFlag handling
///   - Full message replay from journal
///   - Buffering out-of-sequence messages
class FixGapHandler {
public:
    /// Check if an inbound sequence number represents a gap.
    /// Returns true if seq > expected (gap detected).
    static bool is_gap(uint64_t received_seq, uint64_t expected_seq);

    /// Build a ResendRequest message (35=2).
    /// Requests retransmission of messages from begin_seq to end_seq.
    /// end_seq=0 means "infinity" (all messages from begin_seq).
    static std::string build_resend_request(uint64_t begin_seq, uint64_t end_seq,
                                             const std::string& sender, const std::string& target,
                                             uint64_t msg_seq);

    /// Build a SequenceReset-GapFill message (35=4, 123=Y).
    /// Tells the counterparty to advance their expected seq to new_seq.
    static std::string build_gap_fill(uint64_t gap_fill_to, uint64_t msg_seq_num,
                                       const std::string& sender, const std::string& target);

    /// Build a SequenceReset-Reset message (35=4, 123=N).
    /// Hard reset of sequence numbers.
    static std::string build_sequence_reset(uint64_t new_seq,
                                             const std::string& sender, const std::string& target,
                                             uint64_t msg_seq);

    /// Parse a ResendRequest (35=2) and extract begin/end sequence numbers.
    struct ResendRange {
        uint64_t begin_seq = 0;
        uint64_t end_seq = 0;  // 0 = infinity
    };
    static std::optional<ResendRange> parse_resend_request(const std::string& fix_message);

    /// Parse a SequenceReset (35=4) message.
    struct SeqResetInfo {
        uint64_t new_seq_no = 0;
        bool is_gap_fill = false;  // 123=Y
    };
    static std::optional<SeqResetInfo> parse_sequence_reset(const std::string& fix_message);
};

}  // namespace gateway
