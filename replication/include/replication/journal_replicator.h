#pragma once
#include "engine/journal.h"
#include <string>
#include <atomic>
#include <cstdint>

namespace replication {

/// Replication mode.
enum class ReplicationMode : uint8_t {
    SYNC  = 0,  // Wait for DR ack before returning (zero RPO)
    ASYNC = 1,  // Send without waiting (near-zero RPO, lower latency)
};

/// Replication statistics.
struct ReplicationStats {
    uint64_t entries_sent = 0;
    uint64_t entries_acked = 0;
    uint64_t bytes_sent = 0;
    uint64_t ack_failures = 0;
    uint64_t last_acked_seq = 0;
    bool connected = false;
};

/// Sends journal entries to a DR receiver over TCP.
///
/// Usage in the daemon:
///   1. Create replicator with DR host/port
///   2. Call connect() to establish connection
///   3. After each journal append, call replicate(entry)
///   4. In sync mode, replicate() blocks until DR acks
///
/// The replicator uses the same binary record format as FileJournal:
///   [RecordHeader: 24 bytes][Payload: N bytes][CRC32: 4 bytes]
class JournalReplicator {
public:
    struct Config {
        std::string dr_host = "127.0.0.1";
        uint16_t    dr_port = 9878;
        ReplicationMode mode = ReplicationMode::SYNC;
        int         connect_timeout_ms = 5000;
        int         ack_timeout_ms = 1000;
    };

    explicit JournalReplicator(const Config& config);
    ~JournalReplicator();

    // Non-copyable
    JournalReplicator(const JournalReplicator&) = delete;
    JournalReplicator& operator=(const JournalReplicator&) = delete;

    /// Connect to the DR receiver. Returns false on failure.
    bool connect();

    /// Disconnect from the DR receiver.
    void disconnect();

    /// Replicate a journal entry to DR.
    /// In SYNC mode, blocks until ack received.
    /// Returns true if entry was sent (and acked in sync mode).
    bool replicate(const engine::JournalEntry& entry);

    /// Get current replication statistics.
    ReplicationStats stats() const;

    /// Is connected to DR?
    bool is_connected() const { return connected_.load(); }

private:
    Config config_;
    int socket_fd_ = -1;
    std::atomic<bool> connected_{false};
    ReplicationStats stats_;

    // Wire format: same as FileJournal
    static constexpr size_t HEADER_SIZE = 24;
    static constexpr size_t CRC_SIZE = 4;
    struct RecordHeader {
        uint64_t sequence;
        uint64_t timestamp;
        uint16_t partition_id;
        uint16_t entry_type;
        uint32_t payload_size;
    };

    bool send_all(const void* data, size_t len);
    bool recv_ack();
    static uint32_t compute_crc32(const void* data, size_t len);
};

}  // namespace replication
