#include "engine/journal_direct_io.h"

#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <vector>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace engine {

// ─── CRC32 (same table-based algorithm as FileJournal) ───────

static uint32_t dio_crc32_table[256];
static bool dio_crc32_initialized = false;

static void init_dio_crc32_table() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0);
        }
        dio_crc32_table[i] = crc;
    }
    dio_crc32_initialized = true;
}

uint32_t DirectIOJournal::compute_crc32(const void* data, size_t len) {
    if (!dio_crc32_initialized) init_dio_crc32_table();

    auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc = dio_crc32_table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

// ─── Constructor / Destructor ────────────────────────────────

DirectIOJournal::DirectIOJournal(const Config& config) : config_(config) {}

DirectIOJournal::~DirectIOJournal() { close(); }

// ─── Segment path generation ─────────────────────────────────

std::string DirectIOJournal::segment_path(uint32_t index) const {
    std::ostringstream oss;
    oss << config_.base_path << "/segment_"
        << std::setw(6) << std::setfill('0') << index << ".journal";
    return oss.str();
}

// ─── Aligned buffer management ───────────────────────────────

bool DirectIOJournal::allocate_aligned_buffer(size_t size) {
    if (aligned_buf_ && aligned_buf_size_ >= size) return true;

    if (aligned_buf_) {
        free(aligned_buf_);
        aligned_buf_ = nullptr;
        aligned_buf_size_ = 0;
    }

    // Round up to alignment boundary
    size_t aligned_size = ((size + config_.alignment - 1) / config_.alignment) * config_.alignment;

    void* buf = nullptr;
    int ret = posix_memalign(&buf, config_.alignment, aligned_size);
    if (ret != 0 || !buf) return false;

    aligned_buf_ = buf;
    aligned_buf_size_ = aligned_size;
    return true;
}

// ─── Write with alignment padding ────────────────────────────

bool DirectIOJournal::write_aligned(const void* data, size_t len) {
    if (fd_ < 0) return false;

#ifdef __linux__
    // Linux O_DIRECT: both buffer and write size must be aligned
    size_t aligned_len = ((len + config_.alignment - 1) / config_.alignment) * config_.alignment;

    if (!allocate_aligned_buffer(aligned_len)) return false;

    std::memset(aligned_buf_, 0, aligned_len);
    std::memcpy(aligned_buf_, data, len);

    ssize_t written = ::write(fd_, aligned_buf_, aligned_len);
    return (written == static_cast<ssize_t>(aligned_len));
#else
    // macOS F_NOCACHE: aligned buffer is sufficient, no size alignment needed.
    // Write exact record bytes so reads can parse records contiguously.
    if (!allocate_aligned_buffer(len)) return false;

    std::memcpy(aligned_buf_, data, len);

    ssize_t written = ::write(fd_, aligned_buf_, len);
    return (written == static_cast<ssize_t>(len));
#endif
}

// ─── Open ────────────────────────────────────────────────────

bool DirectIOJournal::open() {
    std::filesystem::create_directories(config_.base_path);

    segments_.clear();
    total_entries_ = 0;
    last_seq_ = 0;
    current_segment_index_ = 0;

    // Discover existing segments
    std::vector<std::string> existing;
    for (const auto& entry : std::filesystem::directory_iterator(config_.base_path)) {
        if (entry.path().extension() == ".journal") {
            existing.push_back(entry.path().string());
        }
    }
    std::sort(existing.begin(), existing.end());

    // Load metadata from existing segments
    for (const auto& path : existing) {
        SegmentInfo info;
        info.path = path;
        info.entry_count = count_valid_records(path);

        // Determine first_seq and last_seq by scanning
        if (info.entry_count > 0) {
            SequenceNum first = 0;
            SequenceNum last = 0;
            FILE* f = fopen(path.c_str(), "rb");
            if (f) {
                RecordHeader hdr{};
                bool first_set = false;
                while (fread(&hdr, HEADER_SIZE, 1, f) == 1) {
                    if (hdr.payload_size > 64 * 1024 * 1024) break;  // Sanity check
                    if (!first_set) { first = hdr.sequence; first_set = true; }
                    last = hdr.sequence;
                    // Skip payload + CRC + any alignment padding
                    // For reads we just skip the raw record bytes
                    if (fseek(f, static_cast<long>(hdr.payload_size + CRC_SIZE), SEEK_CUR) != 0) break;
                }
                fclose(f);
            }
            info.first_seq = first;
            info.last_seq = last;
        }

        struct stat st{};
        if (::stat(path.c_str(), &st) == 0) {
            info.file_size = static_cast<uint64_t>(st.st_size);
        }

        total_entries_ += info.entry_count;
        if (info.last_seq > last_seq_) last_seq_ = info.last_seq;

        segments_.push_back(info);
    }

    current_segment_index_ = static_cast<uint32_t>(segments_.size());

    // Open (or create) the active segment
    std::string active_path = segment_path(current_segment_index_);
    if (!open_segment(active_path)) return false;

    // If opening an existing active segment, scan it for existing entries
    struct stat st{};
    if (::fstat(fd_, &st) == 0) {
        current_segment_size_ = static_cast<uint64_t>(st.st_size);
    }

    is_open_ = true;
    return true;
}

// ─── Open a single segment ──────────────────────────────────

bool DirectIOJournal::open_segment(const std::string& path) {
    int flags = O_WRONLY | O_CREAT | O_APPEND;

#ifdef __linux__
    flags |= O_DIRECT;
#endif

    int new_fd = ::open(path.c_str(), flags, 0644);
    if (new_fd < 0) return false;

#ifdef __APPLE__
    // macOS: disable page cache via F_NOCACHE
    fcntl(new_fd, F_NOCACHE, 1);
#endif

    fd_ = new_fd;
    return true;
}

// ─── Close current segment ───────────────────────────────────

void DirectIOJournal::close_current_segment() {
    if (fd_ >= 0) {
        fsync(fd_);
        ::close(fd_);
        fd_ = -1;
    }
}

// ─── Close ───────────────────────────────────────────────────

void DirectIOJournal::close() {
    close_current_segment();

    if (aligned_buf_) {
        free(aligned_buf_);
        aligned_buf_ = nullptr;
        aligned_buf_size_ = 0;
    }

    is_open_ = false;
}

// ─── Segment rotation ────────────────────────────────────────

bool DirectIOJournal::rotate_segment() {
    // Record info about the completed segment
    SegmentInfo info;
    info.path = segment_path(current_segment_index_);
    info.entry_count = 0;  // Will be set below
    info.file_size = current_segment_size_;

    // Count entries and find first/last seq in completed segment
    info.entry_count = count_valid_records(info.path);
    if (info.entry_count > 0) {
        FILE* f = fopen(info.path.c_str(), "rb");
        if (f) {
            RecordHeader hdr{};
            bool first_set = false;
            while (fread(&hdr, HEADER_SIZE, 1, f) == 1) {
                if (!first_set) { info.first_seq = hdr.sequence; first_set = true; }
                info.last_seq = hdr.sequence;
                if (fseek(f, static_cast<long>(hdr.payload_size + CRC_SIZE), SEEK_CUR) != 0) break;
            }
            fclose(f);
        }
    }

    segments_.push_back(info);
    close_current_segment();

    ++current_segment_index_;
    current_segment_size_ = 0;

    std::string new_path = segment_path(current_segment_index_);
    return open_segment(new_path);
}

// ─── Append ──────────────────────────────────────────────────

JournalResult DirectIOJournal::append(const JournalEntry& entry) {
    if (!is_open_ || fd_ < 0) return JournalResult::IO_ERROR;

    // Check if rotation needed
    if (current_segment_size_ >= config_.max_segment_bytes) {
        if (!rotate_segment()) return JournalResult::IO_ERROR;
    }

    // Build the record: header + payload + CRC
    size_t record_size = HEADER_SIZE + entry.payload_size + CRC_SIZE;
    std::vector<uint8_t> record(record_size);

    RecordHeader hdr{};
    hdr.sequence = entry.sequence;
    hdr.timestamp = entry.timestamp;
    hdr.partition_id = entry.partition_id;
    hdr.entry_type = entry.entry_type;
    hdr.payload_size = entry.payload_size;

    std::memcpy(record.data(), &hdr, HEADER_SIZE);
    if (entry.payload_size > 0 && entry.payload) {
        std::memcpy(record.data() + HEADER_SIZE, entry.payload, entry.payload_size);
    }

    // CRC over header + payload
    uint32_t crc = compute_crc32(record.data(), HEADER_SIZE + entry.payload_size);
    std::memcpy(record.data() + HEADER_SIZE + entry.payload_size, &crc, CRC_SIZE);

    // Write using aligned I/O
    if (!write_aligned(record.data(), record_size)) return JournalResult::IO_ERROR;

    // fsync for durability
    fsync(fd_);

    current_segment_size_ += record_size;  // Track logical size (not padded)
    last_seq_ = entry.sequence;
    ++total_entries_;

    return JournalResult::OK;
}

// ─── Query ───────────────────────────────────────────────────

SequenceNum DirectIOJournal::last_sequence() const noexcept { return last_seq_; }
uint64_t DirectIOJournal::entry_count() const noexcept { return total_entries_; }

void DirectIOJournal::flush() {
    if (fd_ >= 0) fsync(fd_);
}

uint32_t DirectIOJournal::segment_count() const {
    // Completed segments + the active segment (if open)
    return static_cast<uint32_t>(segments_.size()) + (is_open_ ? 1 : 0);
}

// ─── Replay ──────────────────────────────────────────────────

uint64_t DirectIOJournal::replay_segment(const std::string& path, SequenceNum from_seq,
                                          Visitor visitor, void* context) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return 0;

    uint64_t visited = 0;
    RecordHeader hdr{};

    while (fread(&hdr, HEADER_SIZE, 1, f) == 1) {
        if (hdr.payload_size > 64 * 1024 * 1024) break;  // Sanity check

        std::vector<uint8_t> payload(hdr.payload_size);
        if (hdr.payload_size > 0) {
            if (fread(payload.data(), hdr.payload_size, 1, f) != 1) break;
        }

        uint32_t stored_crc = 0;
        if (fread(&stored_crc, CRC_SIZE, 1, f) != 1) break;

        // Verify CRC
        std::vector<uint8_t> crc_buf(HEADER_SIZE + hdr.payload_size);
        std::memcpy(crc_buf.data(), &hdr, HEADER_SIZE);
        if (hdr.payload_size > 0) {
            std::memcpy(crc_buf.data() + HEADER_SIZE, payload.data(), hdr.payload_size);
        }
        uint32_t computed_crc = compute_crc32(crc_buf.data(), crc_buf.size());
        if (computed_crc != stored_crc) break;  // Corruption — stop

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

    fclose(f);
    return visited;
}

uint64_t DirectIOJournal::replay_from(SequenceNum from_seq, Visitor visitor, void* context) const {
    uint64_t total = 0;

    // Replay from completed segments
    for (const auto& info : segments_) {
        if (info.entry_count > 0 && info.last_seq < from_seq) continue;
        total += replay_segment(info.path, from_seq, visitor, context);
    }

    // Replay from active segment
    if (is_open_) {
        std::string active = segment_path(current_segment_index_);
        total += replay_segment(active, from_seq, visitor, context);
    }

    return total;
}

// ─── Integrity Verification ──────────────────────────────────

uint64_t DirectIOJournal::count_valid_records(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return 0;

    uint64_t valid = 0;
    RecordHeader hdr{};

    while (fread(&hdr, HEADER_SIZE, 1, f) == 1) {
        if (hdr.payload_size > 64 * 1024 * 1024) break;  // Sanity

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

        ++valid;
    }

    fclose(f);
    return valid;
}

uint64_t DirectIOJournal::verify_integrity() const {
    uint64_t valid = 0;

    for (const auto& info : segments_) {
        valid += count_valid_records(info.path);
    }

    // Active segment
    if (is_open_) {
        valid += count_valid_records(segment_path(current_segment_index_));
    }

    return valid;
}

}  // namespace engine
