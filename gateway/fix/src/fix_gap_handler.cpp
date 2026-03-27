#include "gateway/fix_gap_handler.h"
#include "gateway/fix_parser.h"

#include <sstream>

namespace gateway {

// FIX tag constants for gap handling
namespace {
    constexpr int TAG_BEGIN_STRING  = 8;
    constexpr int TAG_MSG_TYPE      = 35;
    constexpr int TAG_SENDER_COMP   = 49;
    constexpr int TAG_TARGET_COMP   = 56;
    constexpr int TAG_MSG_SEQ_NUM   = 34;
    constexpr int TAG_BEGIN_SEQ_NO  = 7;
    constexpr int TAG_END_SEQ_NO    = 16;
    constexpr int TAG_NEW_SEQ_NO    = 36;
    constexpr int TAG_GAP_FILL_FLAG = 123;
    constexpr int TAG_CHECKSUM      = 10;
    constexpr char SOH = '\x01';
}

bool FixGapHandler::is_gap(uint64_t received_seq, uint64_t expected_seq) {
    return received_seq > expected_seq;
}

std::string FixGapHandler::build_resend_request(uint64_t begin_seq, uint64_t end_seq,
                                                  const std::string& sender, const std::string& target,
                                                  uint64_t msg_seq) {
    std::ostringstream msg;
    msg << TAG_BEGIN_STRING << "=FIX.4.4" << SOH
        << TAG_MSG_TYPE << "=2" << SOH
        << TAG_SENDER_COMP << "=" << sender << SOH
        << TAG_TARGET_COMP << "=" << target << SOH
        << TAG_MSG_SEQ_NUM << "=" << msg_seq << SOH
        << TAG_BEGIN_SEQ_NO << "=" << begin_seq << SOH
        << TAG_END_SEQ_NO << "=" << end_seq << SOH
        << TAG_CHECKSUM << "=000" << SOH;
    return msg.str();
}

std::string FixGapHandler::build_gap_fill(uint64_t gap_fill_to, uint64_t msg_seq_num,
                                            const std::string& sender, const std::string& target) {
    std::ostringstream msg;
    msg << TAG_BEGIN_STRING << "=FIX.4.4" << SOH
        << TAG_MSG_TYPE << "=4" << SOH
        << TAG_SENDER_COMP << "=" << sender << SOH
        << TAG_TARGET_COMP << "=" << target << SOH
        << TAG_MSG_SEQ_NUM << "=" << msg_seq_num << SOH
        << TAG_GAP_FILL_FLAG << "=Y" << SOH
        << TAG_NEW_SEQ_NO << "=" << gap_fill_to << SOH
        << TAG_CHECKSUM << "=000" << SOH;
    return msg.str();
}

std::string FixGapHandler::build_sequence_reset(uint64_t new_seq,
                                                  const std::string& sender, const std::string& target,
                                                  uint64_t msg_seq) {
    std::ostringstream msg;
    msg << TAG_BEGIN_STRING << "=FIX.4.4" << SOH
        << TAG_MSG_TYPE << "=4" << SOH
        << TAG_SENDER_COMP << "=" << sender << SOH
        << TAG_TARGET_COMP << "=" << target << SOH
        << TAG_MSG_SEQ_NUM << "=" << msg_seq << SOH
        << TAG_GAP_FILL_FLAG << "=N" << SOH
        << TAG_NEW_SEQ_NO << "=" << new_seq << SOH
        << TAG_CHECKSUM << "=000" << SOH;
    return msg.str();
}

std::optional<FixGapHandler::ResendRange> FixGapHandler::parse_resend_request(const std::string& fix_message) {
    auto fields = FixParser::parse_fields(fix_message);

    // Verify this is a ResendRequest (35=2)
    auto msg_type = FixParser::get_msg_type(fields);
    if (!msg_type || *msg_type != "2") return std::nullopt;

    auto begin_it = fields.find(TAG_BEGIN_SEQ_NO);
    if (begin_it == fields.end()) return std::nullopt;

    ResendRange range;
    range.begin_seq = std::stoull(begin_it->second);

    auto end_it = fields.find(TAG_END_SEQ_NO);
    if (end_it != fields.end()) {
        range.end_seq = std::stoull(end_it->second);
    }

    return range;
}

std::optional<FixGapHandler::SeqResetInfo> FixGapHandler::parse_sequence_reset(const std::string& fix_message) {
    auto fields = FixParser::parse_fields(fix_message);

    // Verify this is a SequenceReset (35=4)
    auto msg_type = FixParser::get_msg_type(fields);
    if (!msg_type || *msg_type != "4") return std::nullopt;

    auto new_seq_it = fields.find(TAG_NEW_SEQ_NO);
    if (new_seq_it == fields.end()) return std::nullopt;

    SeqResetInfo info;
    info.new_seq_no = std::stoull(new_seq_it->second);

    auto gap_fill_it = fields.find(TAG_GAP_FILL_FLAG);
    if (gap_fill_it != fields.end()) {
        info.is_gap_fill = (gap_fill_it->second == "Y");
    }

    return info;
}

}  // namespace gateway
