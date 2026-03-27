#include "engine/journal_segmented.h"
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <iomanip>

namespace engine {

SegmentedJournal::SegmentedJournal(const Config& config) : config_(config) {}

SegmentedJournal::~SegmentedJournal() { close(); }

std::string SegmentedJournal::segment_path(uint32_t index) const {
    std::ostringstream oss;
    oss << config_.base_path << "/segment_"
        << std::setw(6) << std::setfill('0') << index << ".journal";
    return oss.str();
}

bool SegmentedJournal::open() {
    std::filesystem::create_directories(config_.base_path);

    // Discover existing segments
    segments_.clear();
    total_entries_ = 0;
    last_seq_ = 0;

    std::vector<std::string> existing;
    for (const auto& entry : std::filesystem::directory_iterator(config_.base_path)) {
        if (entry.path().extension() == ".journal") {
            existing.push_back(entry.path().string());
        }
    }
    std::sort(existing.begin(), existing.end());

    // Load metadata from existing segments
    for (const auto& path : existing) {
        FileJournal seg;
        if (!seg.open(path, FileJournal::FlushMode::NONE)) continue;

        SegmentInfo info;
        info.path = path;
        info.entry_count = seg.entry_count();
        info.last_seq = seg.last_sequence();
        info.file_size = seg.file_size();

        // Determine first_seq by replaying first entry
        if (info.entry_count > 0) {
            SequenceNum first = 0;
            seg.replay_from(0, [](const JournalEntry& e, void* ctx) {
                auto* f = static_cast<SequenceNum*>(ctx);
                if (*f == 0) *f = e.sequence;
            }, &first);
            info.first_seq = first;
        }

        total_entries_ += info.entry_count;
        if (info.last_seq > last_seq_) last_seq_ = info.last_seq;

        seg.close();
        segments_.push_back(info);
    }

    // Open or create active segment
    if (!open_active_segment()) return false;

    is_open_ = true;
    return true;
}

bool SegmentedJournal::open_active_segment() {
    std::string path = segment_path(static_cast<uint32_t>(segments_.size()));

    if (!active_segment_.open(path, config_.flush_mode)) return false;

    // If opening an existing file, update metadata
    if (active_segment_.entry_count() > 0) {
        total_entries_ += active_segment_.entry_count();
        last_seq_ = active_segment_.last_sequence();
    }

    return true;
}

bool SegmentedJournal::rotate_segment() {
    // Record info about the completed segment
    SegmentInfo info;
    info.path = segment_path(static_cast<uint32_t>(segments_.size()));
    info.entry_count = active_segment_.entry_count();
    info.last_seq = active_segment_.last_sequence();
    info.file_size = active_segment_.file_size();
    segments_.push_back(info);

    active_segment_.close();

    // Open new segment
    std::string new_path = segment_path(static_cast<uint32_t>(segments_.size()));
    return active_segment_.open(new_path, config_.flush_mode);
}

void SegmentedJournal::close() {
    if (is_open_) {
        active_segment_.close();
        is_open_ = false;
    }
}

JournalResult SegmentedJournal::append(const JournalEntry& entry) {
    if (!is_open_) return JournalResult::IO_ERROR;

    // Check if rotation needed
    if (active_segment_.file_size() >= config_.max_segment_bytes) {
        if (!rotate_segment()) return JournalResult::IO_ERROR;
    }

    auto result = active_segment_.append(entry);
    if (result == JournalResult::OK) {
        ++total_entries_;
        last_seq_ = entry.sequence;
    }
    return result;
}

SequenceNum SegmentedJournal::last_sequence() const noexcept { return last_seq_; }
uint64_t SegmentedJournal::entry_count() const noexcept { return total_entries_; }

void SegmentedJournal::flush() {
    if (is_open_) active_segment_.flush();
}

uint64_t SegmentedJournal::replay_from(SequenceNum from_seq, Visitor visitor, void* context) const {
    uint64_t total = 0;

    // Replay from completed segments
    for (const auto& info : segments_) {
        if (info.last_seq < from_seq) continue;  // Skip segments entirely before from_seq

        FileJournal seg;
        if (!seg.open(info.path, FileJournal::FlushMode::NONE)) continue;
        total += seg.replay_from(from_seq, visitor, context);
        seg.close();
    }

    // Replay from active segment
    if (is_open_) {
        total += active_segment_.replay_from(from_seq, visitor, context);
    }

    return total;
}

uint64_t SegmentedJournal::verify_integrity() const {
    uint64_t valid = 0;
    for (const auto& info : segments_) {
        FileJournal seg;
        if (!seg.open(info.path, FileJournal::FlushMode::NONE)) continue;
        valid += seg.verify_integrity();
        seg.close();
    }
    if (is_open_) {
        valid += active_segment_.verify_integrity();
    }
    return valid;
}

}  // namespace engine
