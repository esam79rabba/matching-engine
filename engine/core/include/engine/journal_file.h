#pragma once

#include "engine/journal.h"
#include <string>
#include <fstream>
#include <vector>
#include <cstdint>

namespace engine {

/// File-backed local journal — first durable prototype.
///
/// Appends entries to a local file with a simple binary format:
///   [RecordHeader][Payload][CRC32]
///
/// RecordHeader:
///   sequence:     uint64 (8 bytes)
///   timestamp:    uint64 (8 bytes)
///   partition_id: uint16 (2 bytes)
///   entry_type:   uint16 (2 bytes)
///   payload_size: uint32 (4 bytes)
///   --- total: 24 bytes ---
///
/// After payload: CRC32 checksum (4 bytes).
///
/// Durability:
///   - flush_mode = SYNC: fflush + fsync after each append (Regulatory-Grade path)
///   - flush_mode = BATCH: fflush only; fsync deferred (Reduced-Latency path)
///   - flush_mode = NONE: no explicit flush (development only)
///
/// This is a PROTOTYPE. Production differences:
///   - Production uses Direct I/O (O_DIRECT) bypassing page cache
///   - Production uses io_uring or raw syscalls, not std::fstream
///   - Production implements segment rotation and compression
///   - Production has replication to DR site
///
/// The interface (IJournal) is identical, so the swap is transparent.
class FileJournal : public IJournal {
public:
    enum class FlushMode : uint8_t {
        SYNC  = 0,  // fsync after every append (Regulatory-Grade)
        BATCH = 1,  // fflush only; fsync on explicit flush() call
        NONE  = 2,  // No explicit flush (dev/test)
    };

    /// Open or create a journal file at the given path.
    /// Returns false if file cannot be opened.
    bool open(const std::string& path, FlushMode mode = FlushMode::SYNC);

    /// Close the journal file.
    void close();

    // ─── IJournal interface ─────────────────────────────────

    JournalResult append(const JournalEntry& entry) override;
    [[nodiscard]] SequenceNum last_sequence() const noexcept override;
    [[nodiscard]] uint64_t entry_count() const noexcept override;
    uint64_t replay_from(SequenceNum from_seq, Visitor visitor, void* context) const override;
    void flush() override;

    // ─── Query ──────────────────────────────────────────────

    [[nodiscard]] bool is_open() const noexcept { return is_open_; }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] uint64_t file_size() const noexcept { return file_size_; }

    /// Verify integrity by reading all records and checking CRC32.
    /// Returns the number of valid records found.
    [[nodiscard]] uint64_t verify_integrity() const;

private:
    std::string    path_;
    FlushMode      mode_ = FlushMode::SYNC;
    bool           is_open_ = false;
    FILE*          file_ = nullptr;
    SequenceNum    last_seq_ = 0;
    uint64_t       count_ = 0;
    uint64_t       file_size_ = 0;

    static constexpr size_t HEADER_SIZE = 24;
    static constexpr size_t CRC_SIZE = 4;

    struct RecordHeader {
        uint64_t sequence;
        uint64_t timestamp;
        uint16_t partition_id;
        uint16_t entry_type;
        uint32_t payload_size;
    };

    /// Simple CRC32 (not cryptographic — for corruption detection only).
    static uint32_t compute_crc32(const void* data, size_t len);
};

}  // namespace engine
