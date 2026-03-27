#pragma once

#include "engine/journal.h"
#include "engine/journal_file.h"
#include <string>
#include <vector>
#include <cstdint>

namespace engine {

/// Segmented journal — production-oriented local journal with rotation.
///
/// Wraps FileJournal with automatic segment rotation:
///   - New segment created when current exceeds max_segment_bytes
///   - Segments named: {base_path}/segment_{NNNNNN}.journal
///   - Supports replay across segment boundaries
///   - Maintains a manifest file listing all segments
///
/// Regulatory-Grade / Reduced-Latency flush modes are passed through
/// to the underlying FileJournal.
///
/// Still local durability only — DR replication is Phase 2+.
/// Still uses stdio (not O_DIRECT) — kernel-bypass I/O is Phase 2+.
///
/// Generic — no asset-class-specific logic.
class SegmentedJournal : public IJournal {
public:
    struct Config {
        std::string base_path;
        uint64_t    max_segment_bytes = 64 * 1024 * 1024;  // 64 MB default
        FileJournal::FlushMode flush_mode = FileJournal::FlushMode::SYNC;
    };

    explicit SegmentedJournal(const Config& config);
    ~SegmentedJournal();

    /// Open the journal directory. Creates if needed. Loads existing segments.
    bool open();

    /// Close all segments.
    void close();

    // ─── IJournal interface ─────────────────────────────────
    JournalResult append(const JournalEntry& entry) override;
    [[nodiscard]] SequenceNum last_sequence() const noexcept override;
    [[nodiscard]] uint64_t entry_count() const noexcept override;
    uint64_t replay_from(SequenceNum from_seq, Visitor visitor, void* context) const override;
    void flush() override;

    // ─── Segment management ─────────────────────────────────
    [[nodiscard]] uint32_t segment_count() const { return static_cast<uint32_t>(segments_.size()); }
    [[nodiscard]] bool is_open() const noexcept { return is_open_; }

    /// Verify integrity of all segments. Returns total valid entries.
    [[nodiscard]] uint64_t verify_integrity() const;

private:
    Config config_;
    bool is_open_ = false;
    uint64_t total_entries_ = 0;
    SequenceNum last_seq_ = 0;

    struct SegmentInfo {
        std::string path;
        SequenceNum first_seq = 0;
        SequenceNum last_seq = 0;
        uint64_t entry_count = 0;
        uint64_t file_size = 0;
    };

    std::vector<SegmentInfo> segments_;
    FileJournal active_segment_;

    std::string segment_path(uint32_t index) const;
    bool rotate_segment();
    bool open_active_segment();
};

}  // namespace engine
