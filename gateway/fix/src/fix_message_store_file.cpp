#include "gateway/fix_message_store_file.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>

namespace gateway {

static const std::string kFileExtension = ".msgstore";

FileMessageStore::FileMessageStore(const std::string& dir, uint32_t max_per_session)
    : dir_(dir), store_(max_per_session) {
    std::filesystem::create_directories(dir_);
}

FileMessageStore::~FileMessageStore() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    for (auto& [_, stream] : file_streams_) {
        if (stream.is_open()) {
            stream.flush();
            stream.close();
        }
    }
}

std::string FileMessageStore::session_file_path(const std::string& sender_comp_id) const {
    return dir_ + "/" + sender_comp_id + kFileExtension;
}

std::ofstream& FileMessageStore::get_stream(const std::string& sender_comp_id) {
    auto it = file_streams_.find(sender_comp_id);
    if (it != file_streams_.end() && it->second.is_open()) {
        return it->second;
    }

    auto path = session_file_path(sender_comp_id);
    auto& stream = file_streams_[sender_comp_id];
    stream.open(path, std::ios::binary | std::ios::app);
    return stream;
}

void FileMessageStore::store(const std::string& sender_comp_id, uint64_t seq_num,
                              const std::string& fix_message) {
    // Persist to file
    {
        std::lock_guard<std::mutex> lock(file_mutex_);
        auto& stream = get_stream(sender_comp_id);
        if (stream.is_open()) {
            uint32_t msg_len = static_cast<uint32_t>(fix_message.size());
            stream.write(reinterpret_cast<const char*>(&seq_num), sizeof(seq_num));
            stream.write(reinterpret_cast<const char*>(&msg_len), sizeof(msg_len));
            stream.write(fix_message.data(), msg_len);
            stream.flush();
        }
    }

    // Store in memory
    store_.store(sender_comp_id, seq_num, fix_message);
}

std::optional<std::string> FileMessageStore::get(const std::string& sender_comp_id,
                                                   uint64_t seq_num) const {
    return store_.get(sender_comp_id, seq_num);
}

std::vector<std::pair<uint64_t, std::string>> FileMessageStore::get_range(
    const std::string& sender_comp_id,
    uint64_t begin_seq, uint64_t end_seq) const {
    return store_.get_range(sender_comp_id, begin_seq, end_seq);
}

void FileMessageStore::clear(const std::string& sender_comp_id) {
    store_.clear(sender_comp_id);
    // Note: file is NOT deleted — only in-memory state cleared.
    // On next load(), the file would repopulate. For a full reset,
    // the operator should delete the file manually or call a purge method.
}

uint32_t FileMessageStore::count(const std::string& sender_comp_id) const {
    return store_.count(sender_comp_id);
}

uint32_t FileMessageStore::load() {
    uint32_t total = 0;

    if (!std::filesystem::exists(dir_)) return 0;

    for (const auto& entry : std::filesystem::directory_iterator(dir_)) {
        if (!entry.is_regular_file()) continue;

        auto path = entry.path();
        if (path.extension() != kFileExtension) continue;

        // Extract sender_comp_id from filename (stem without extension)
        std::string sender_comp_id = path.stem().string();
        total += load_session_file(path.string(), sender_comp_id);
    }

    return total;
}

uint32_t FileMessageStore::load_session_file(const std::string& path,
                                               const std::string& sender_comp_id) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return 0;

    uint32_t count = 0;

    while (in.good() && !in.eof()) {
        uint64_t seq_num = 0;
        uint32_t msg_len = 0;

        in.read(reinterpret_cast<char*>(&seq_num), sizeof(seq_num));
        if (!in.good()) break;

        in.read(reinterpret_cast<char*>(&msg_len), sizeof(msg_len));
        if (!in.good()) break;

        // Sanity check: reject absurdly large messages (>10MB)
        if (msg_len > 10 * 1024 * 1024) {
            std::cerr << "[FileMessageStore] Corrupt record in " << path
                      << " at seq=" << seq_num << " msg_len=" << msg_len << "\n";
            break;
        }

        std::string message(msg_len, '\0');
        in.read(message.data(), msg_len);
        if (!in.good() && !in.eof()) break;

        // Use the base store directly — no file write needed during load
        store_.store(sender_comp_id, seq_num, message);
        ++count;
    }

    return count;
}

}  // namespace gateway
