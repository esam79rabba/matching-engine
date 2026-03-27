#pragma once

#include "engine/journal.h"
#include <string>
#include <vector>
#include <cstdint>

namespace engine {

/// Production journal using Direct I/O (O_DIRECT on Linux, F_NOCACHE on macOS).
///
/// Bypasses the kernel page cache for guaranteed durability:
///   - Writes go directly to storage device
///   - No risk of data loss in page cache on power failure
///   - Requires aligned buffers (typically 512-byte or 4K alignment)
///
/// Implements the same IJournal interface as FileJournal, so it's a
/// drop-in replacement selectable via config.
///
/// Supports segmented rotation (like SegmentedJournal) built-in.
///
/// Record format is identical to FileJournal for cross-compatibility:
///   [RecordHeader (24 bytes)][Payload][CRC32 (4 bytes)]
///
/// Config selection: journal.type=direct_io in platform.conf
class DirectIOJournal : public IJournal {
public:
    struct Config {
        std::string base_path;
        uint64_t    max_segment_bytes = 64 * 1024 * 1024;  // 64 MB default
        uint32_t    alignment = 4096;  // Buffer alignment for Direct I/O
    };

    explicit DirectIOJournal(const Config& config);
    ~DirectIOJournal();

    /// Open the journal directory. Creates if needed. Discovers existing segments.
    bool open();

    /// Close all segments and free aligned buffer.
    void close();

    // ─── IJournal interface ─────────────────────────────────
    JournalResult append(const JournalEntry& entry) override;
    [[nodiscard]] SequenceNum last_sequence() const noexcept override;
    [[nodiscard]] uint64_t entry_count() const noexcept override;
    uint64_t replay_from(SequenceNum from_seq, Visitor visitor, void* context) const override;
    void flush() override;

    // ─── Query ──────────────────────────────────────────────
    [[nodiscard]] bool is_open() const noexcept { return is_open_; }
    [[nodiscard]] uint32_t segment_count() const;
    [[nodiscard]] uint64_t verify_integrity() const;

private:
    Config config_;
    bool is_open_ = false;
    int fd_ = -1;  // Current segment file descriptor
    uint64_t total_entries_ = 0;
    SequenceNum last_seq_ = 0;
    uint64_t current_segment_size_ = 0;
    uint32_t current_segment_index_ = 0;

    // Aligned write buffer
    void* aligned_buf_ = nullptr;
    size_t aligned_buf_size_ = 0;

    // Segment tracking
    struct SegmentInfo {
        std::string path;
        SequenceNum first_seq = 0;
        SequenceNum last_seq = 0;
        uint64_t entry_count = 0;
        uint64_t file_size = 0;
    };
    std::vector<SegmentInfo> segments_;

    std::string segment_path(uint32_t index) const;
    bool rotate_segment();
    bool open_segment(const std::string& path);
    void close_current_segment();

    // Aligned I/O helpers
    bool allocate_aligned_buffer(size_t size);
    bool write_aligned(const void* data, size_t len);

    // CRC32 (same algorithm as FileJournal for compatibility)
    static uint32_t compute_crc32(const void* data, size_t len);

    // Record format (identical to FileJournal)
    static constexpr size_t HEADER_SIZE = 24;
    static constexpr size_t CRC_SIZE = 4;

    struct RecordHeader {
        uint64_t sequence;
        uint64_t timestamp;
        uint16_t partition_id;
        uint16_t entry_type;
        uint32_t payload_size;
    };

    // Replay a single segment file. Returns entries visited.
    static uint64_t replay_segment(const std::string& path, SequenceNum from_seq,
                                   Visitor visitor, void* context);

    // Count valid records in a segment file.
    static uint64_t count_valid_records(const std::string& path);
};

}  // namespace engine
