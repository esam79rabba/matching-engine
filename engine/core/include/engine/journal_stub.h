#pragma once

#include "engine/journal.h"
#include <vector>

namespace engine {

/// In-memory journal stub for development and testing.
///
/// WARNING: This stub provides NO durability. It stores entries in memory
/// only. On process termination, all data is lost. This is suitable for:
///   - Unit testing
///   - Integration testing
///   - Development bootstrap (Milestone 1)
///   - Deterministic replay testing (within a single process lifetime)
///
/// Production replacement: Custom NVMe append-only journal (D-06 working
/// assumption) implementing IJournal with fsync-before-ack semantics.
///
/// The stub copies the payload bytes into internal storage so entries
/// survive beyond the caller's buffer lifetime. This is NOT how the
/// production journal works (production uses zero-copy Direct I/O).
class JournalStub : public IJournal {
public:
    explicit JournalStub(uint32_t max_entries = 1000000);

    JournalResult append(const JournalEntry& entry) override;

    [[nodiscard]] SequenceNum last_sequence() const noexcept override;
    [[nodiscard]] uint64_t entry_count() const noexcept override;

    uint64_t replay_from(SequenceNum from_seq, Visitor visitor, void* context) const override;

    void flush() override;  // No-op for stub

    /// Direct access to stored entries (testing convenience).
    [[nodiscard]] const std::vector<JournalEntry>& entries() const noexcept { return entries_; }

    /// Access stored payload bytes for an entry (testing convenience).
    [[nodiscard]] const std::vector<uint8_t>& payload_bytes(size_t index) const;

private:
    uint32_t max_entries_;
    SequenceNum last_seq_ = 0;

    // Stored entries (with copied payloads)
    std::vector<JournalEntry> entries_;
    std::vector<std::vector<uint8_t>> payloads_;  // Parallel array of payload copies
};

}  // namespace engine
