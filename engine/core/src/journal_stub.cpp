#include "engine/journal_stub.h"
#include <algorithm>

namespace engine {

JournalStub::JournalStub(uint32_t max_entries)
    : max_entries_(max_entries) {
    entries_.reserve(std::min(max_entries, 10000u));
    payloads_.reserve(std::min(max_entries, 10000u));
}

JournalResult JournalStub::append(const JournalEntry& entry) {
    if (entries_.size() >= max_entries_) {
        return JournalResult::BUFFER_FULL;
    }

    // Copy payload bytes into internal storage.
    // Production journal does NOT copy — it writes directly to NVMe.
    payloads_.emplace_back();
    auto& stored_payload = payloads_.back();
    if (entry.payload != nullptr && entry.payload_size > 0) {
        stored_payload.resize(entry.payload_size);
        std::memcpy(stored_payload.data(), entry.payload, entry.payload_size);
    }

    // Store entry with pointer redirected to our internal copy
    JournalEntry stored = entry;
    stored.payload = stored_payload.empty() ? nullptr : stored_payload.data();
    entries_.push_back(stored);

    last_seq_ = entry.sequence;
    return JournalResult::OK;
}

SequenceNum JournalStub::last_sequence() const noexcept {
    return last_seq_;
}

uint64_t JournalStub::entry_count() const noexcept {
    return entries_.size();
}

uint64_t JournalStub::replay_from(SequenceNum from_seq, Visitor visitor, void* context) const {
    uint64_t count = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].sequence >= from_seq) {
            // Reconstruct entry with valid payload pointer
            JournalEntry replay_entry = entries_[i];
            replay_entry.payload = payloads_[i].empty() ? nullptr : payloads_[i].data();
            visitor(replay_entry, context);
            ++count;
        }
    }
    return count;
}

void JournalStub::flush() {
    // No-op — stub has no pending writes
}

const std::vector<uint8_t>& JournalStub::payload_bytes(size_t index) const {
    return payloads_.at(index);
}

}  // namespace engine
