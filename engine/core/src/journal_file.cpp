#include "engine/journal_file.h"
#include <cstring>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>  // fsync
#endif

namespace engine {

// ─── CRC32 (simple table-based) ─────────────────────────────

static uint32_t crc32_table[256];
static bool crc32_initialized = false;

static void init_crc32_table() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0);
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = true;
}

uint32_t FileJournal::compute_crc32(const void* data, size_t len) {
    if (!crc32_initialized) init_crc32_table();

    auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc = crc32_table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

// ─── Open / Close ───────────────────────────────────────────

bool FileJournal::open(const std::string& path, FlushMode mode) {
    if (is_open_) close();

    path_ = path;
    mode_ = mode;

    // Open for append + read
    file_ = fopen(path.c_str(), "a+b");
    if (!file_) return false;

    is_open_ = true;

    // Determine file size and count existing records
    fseek(file_, 0, SEEK_END);
    file_size_ = static_cast<uint64_t>(ftell(file_));

    // Scan existing records to set last_seq_ and count_
    if (file_size_ > 0) {
        count_ = verify_integrity();
        // Set last_seq_ from the last valid record
        // (verify_integrity returns count; we need to re-scan for last seq)
        fseek(file_, 0, SEEK_SET);
        RecordHeader hdr{};
        while (fread(&hdr, HEADER_SIZE, 1, file_) == 1) {
            // Skip payload + CRC
            if (fseek(file_, hdr.payload_size + CRC_SIZE, SEEK_CUR) != 0) break;
            last_seq_ = hdr.sequence;
        }
        fseek(file_, 0, SEEK_END);  // Return to append position
    }

    return true;
}

void FileJournal::close() {
    if (file_) {
        if (mode_ != FlushMode::NONE) {
            fflush(file_);
#if defined(__unix__) || defined(__APPLE__)
            if (mode_ == FlushMode::SYNC) {
                fsync(fileno(file_));
            }
#endif
        }
        fclose(file_);
        file_ = nullptr;
    }
    is_open_ = false;
}

// ─── Append ─────────────────────────────────────────────────

JournalResult FileJournal::append(const JournalEntry& entry) {
    if (!is_open_ || !file_) return JournalResult::IO_ERROR;

    // Write header
    RecordHeader hdr{};
    hdr.sequence = entry.sequence;
    hdr.timestamp = entry.timestamp;
    hdr.partition_id = entry.partition_id;
    hdr.entry_type = entry.entry_type;
    hdr.payload_size = entry.payload_size;

    if (fwrite(&hdr, HEADER_SIZE, 1, file_) != 1) return JournalResult::IO_ERROR;

    // Write payload
    if (entry.payload_size > 0 && entry.payload != nullptr) {
        if (fwrite(entry.payload, entry.payload_size, 1, file_) != 1) return JournalResult::IO_ERROR;
    }

    // Compute and write CRC32 over header + payload
    std::vector<uint8_t> crc_buf(HEADER_SIZE + entry.payload_size);
    std::memcpy(crc_buf.data(), &hdr, HEADER_SIZE);
    if (entry.payload_size > 0 && entry.payload) {
        std::memcpy(crc_buf.data() + HEADER_SIZE, entry.payload, entry.payload_size);
    }
    uint32_t crc = compute_crc32(crc_buf.data(), crc_buf.size());
    if (fwrite(&crc, CRC_SIZE, 1, file_) != 1) return JournalResult::IO_ERROR;

    // Flush based on mode
    if (mode_ == FlushMode::SYNC) {
        fflush(file_);
#if defined(__unix__) || defined(__APPLE__)
        fsync(fileno(file_));
#endif
    } else if (mode_ == FlushMode::BATCH) {
        fflush(file_);
    }

    file_size_ += HEADER_SIZE + entry.payload_size + CRC_SIZE;
    last_seq_ = entry.sequence;
    ++count_;

    return JournalResult::OK;
}

// ─── Query ──────────────────────────────────────────────────

SequenceNum FileJournal::last_sequence() const noexcept { return last_seq_; }
uint64_t FileJournal::entry_count() const noexcept { return count_; }

void FileJournal::flush() {
    if (!file_) return;
    fflush(file_);
#if defined(__unix__) || defined(__APPLE__)
    fsync(fileno(file_));
#endif
}

// ─── Replay ─────────────────────────────────────────────────

uint64_t FileJournal::replay_from(SequenceNum from_seq, Visitor visitor, void* context) const {
    if (!is_open_) return 0;

    // Open a separate read handle (don't disturb the write position)
    FILE* read_file = fopen(path_.c_str(), "rb");
    if (!read_file) return 0;

    uint64_t visited = 0;
    RecordHeader hdr{};

    while (fread(&hdr, HEADER_SIZE, 1, read_file) == 1) {
        std::vector<uint8_t> payload(hdr.payload_size);
        if (hdr.payload_size > 0) {
            if (fread(payload.data(), hdr.payload_size, 1, read_file) != 1) break;
        }

        uint32_t stored_crc = 0;
        if (fread(&stored_crc, CRC_SIZE, 1, read_file) != 1) break;

        // Verify CRC
        std::vector<uint8_t> crc_buf(HEADER_SIZE + hdr.payload_size);
        std::memcpy(crc_buf.data(), &hdr, HEADER_SIZE);
        if (hdr.payload_size > 0) {
            std::memcpy(crc_buf.data() + HEADER_SIZE, payload.data(), hdr.payload_size);
        }
        uint32_t computed_crc = compute_crc32(crc_buf.data(), crc_buf.size());
        if (computed_crc != stored_crc) break;  // Corruption — stop replay

        if (hdr.sequence >= from_seq) {
            JournalEntry entry{};
            entry.sequence = hdr.sequence;
            entry.timestamp = hdr.timestamp;
            entry.partition_id = hdr.partition_id;
            entry.entry_type = hdr.entry_type;
            entry.payload_size = hdr.payload_size;
            entry.payload = payload.empty() ? nullptr : payload.data();
            visitor(entry, context);
            ++visited;
        }
    }

    fclose(read_file);
    return visited;
}

// ─── Integrity Verification ─────────────────────────────────

uint64_t FileJournal::verify_integrity() const {
    FILE* f = fopen(path_.c_str(), "rb");
    if (!f) return 0;

    uint64_t valid_count = 0;
    RecordHeader hdr{};

    while (fread(&hdr, HEADER_SIZE, 1, f) == 1) {
        std::vector<uint8_t> payload(hdr.payload_size);
        if (hdr.payload_size > 0) {
            if (fread(payload.data(), hdr.payload_size, 1, f) != 1) break;
        }
        uint32_t stored_crc = 0;
        if (fread(&stored_crc, CRC_SIZE, 1, f) != 1) break;

        std::vector<uint8_t> crc_buf(HEADER_SIZE + hdr.payload_size);
        std::memcpy(crc_buf.data(), &hdr, HEADER_SIZE);
        if (hdr.payload_size > 0) {
            std::memcpy(crc_buf.data() + HEADER_SIZE, payload.data(), hdr.payload_size);
        }
        if (compute_crc32(crc_buf.data(), crc_buf.size()) != stored_crc) break;

        ++valid_count;
    }

    fclose(f);
    return valid_count;
}

}  // namespace engine
