#pragma once

#include "gateway/fix_message_store.h"

#include <string>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace gateway {

/// File-backed FIX message store for persistent replay across restarts.
///
/// Uses composition: wraps an in-memory FixMessageStore and adds
/// append-only file persistence per session.
///
/// Storage format (binary, append-only, one file per sender_comp_id):
///   [8 bytes: uint64_t seq_num]
///   [4 bytes: uint32_t msg_len]
///   [msg_len bytes: message data]
///
/// On store(): appends to both memory and file.
/// On load():  reads existing file(s) and populates the in-memory store.
/// On restart: construct a new FileMessageStore on the same directory,
///             call load(), and all persisted messages are available.
///
/// The file grows append-only even when the in-memory store evicts old
/// messages. This is acceptable for prototype; production would compact.
class FileMessageStore {
public:
    /// Create with storage directory and max messages per session (in memory).
    /// The directory is created if it does not exist.
    explicit FileMessageStore(const std::string& dir,
                               uint32_t max_per_session = 10000);

    ~FileMessageStore();

    FileMessageStore(const FileMessageStore&) = delete;
    FileMessageStore& operator=(const FileMessageStore&) = delete;

    /// Load persisted messages from all session files in the directory.
    /// Call once after construction to recover state.
    /// Returns the number of messages loaded.
    uint32_t load();

    /// Store a message (persists to file and to in-memory store).
    void store(const std::string& sender_comp_id, uint64_t seq_num,
               const std::string& fix_message);

    /// Retrieve a stored message from in-memory store.
    std::optional<std::string> get(const std::string& sender_comp_id,
                                    uint64_t seq_num) const;

    /// Retrieve a range of messages from in-memory store.
    std::vector<std::pair<uint64_t, std::string>> get_range(
        const std::string& sender_comp_id,
        uint64_t begin_seq, uint64_t end_seq) const;

    /// Clear a session (in-memory only; file is not deleted).
    void clear(const std::string& sender_comp_id);

    /// Count of in-memory messages for a session.
    uint32_t count(const std::string& sender_comp_id) const;

    /// Get the storage directory path.
    const std::string& directory() const { return dir_; }

    /// Get access to the underlying in-memory store (for acceptor wiring).
    FixMessageStore& inner() { return store_; }
    const FixMessageStore& inner() const { return store_; }

private:
    std::string dir_;
    FixMessageStore store_;
    mutable std::mutex file_mutex_;

    /// Open file streams keyed by sender_comp_id (lazy-opened on first store).
    std::unordered_map<std::string, std::ofstream> file_streams_;

    /// Get the file path for a session.
    std::string session_file_path(const std::string& sender_comp_id) const;

    /// Ensure the output stream for a session is open.
    std::ofstream& get_stream(const std::string& sender_comp_id);

    /// Load a single session file into the in-memory store.
    uint32_t load_session_file(const std::string& path,
                                const std::string& sender_comp_id);
};

}  // namespace gateway
