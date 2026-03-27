#include "gateway/fix_message_store.h"

#include <algorithm>
#include <limits>

namespace gateway {

FixMessageStore::FixMessageStore(uint32_t max_messages_per_session)
    : max_per_session_(max_messages_per_session) {}

void FixMessageStore::store(const std::string& sender_comp_id, uint64_t seq_num,
                             const std::string& fix_message) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& sm = sessions_[sender_comp_id];
    sm.messages[seq_num] = fix_message;

    // Track oldest if this is the first message
    if (sm.messages.size() == 1) {
        sm.oldest_seq = seq_num;
    }

    evict_old(sm);
}

std::optional<std::string> FixMessageStore::get(const std::string& sender_comp_id,
                                                  uint64_t seq_num) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto sit = sessions_.find(sender_comp_id);
    if (sit == sessions_.end()) return std::nullopt;

    auto mit = sit->second.messages.find(seq_num);
    if (mit == sit->second.messages.end()) return std::nullopt;

    return mit->second;
}

std::vector<std::pair<uint64_t, std::string>> FixMessageStore::get_range(
    const std::string& sender_comp_id,
    uint64_t begin_seq, uint64_t end_seq) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::pair<uint64_t, std::string>> result;
    auto sit = sessions_.find(sender_comp_id);
    if (sit == sessions_.end()) return result;

    const auto& msgs = sit->second.messages;
    for (uint64_t seq = begin_seq; seq <= end_seq; ++seq) {
        auto it = msgs.find(seq);
        if (it != msgs.end()) {
            result.emplace_back(seq, it->second);
        }
    }
    return result;
}

void FixMessageStore::clear(const std::string& sender_comp_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(sender_comp_id);
}

uint32_t FixMessageStore::count(const std::string& sender_comp_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto sit = sessions_.find(sender_comp_id);
    if (sit == sessions_.end()) return 0;
    return static_cast<uint32_t>(sit->second.messages.size());
}

void FixMessageStore::evict_old(SessionMessages& sm) {
    while (sm.messages.size() > max_per_session_) {
        // Find and remove the oldest entry
        uint64_t min_seq = std::numeric_limits<uint64_t>::max();
        for (const auto& [seq, _] : sm.messages) {
            if (seq < min_seq) {
                min_seq = seq;
            }
        }
        sm.messages.erase(min_seq);
        // Update oldest_seq to the next minimum
        if (!sm.messages.empty()) {
            uint64_t new_min = std::numeric_limits<uint64_t>::max();
            for (const auto& [seq, _] : sm.messages) {
                if (seq < new_min) {
                    new_min = seq;
                }
            }
            sm.oldest_seq = new_min;
        } else {
            sm.oldest_seq = 0;
        }
    }
}

}  // namespace gateway
