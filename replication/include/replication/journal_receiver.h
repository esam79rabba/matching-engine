#pragma once
#include "engine/journal.h"
#include <string>
#include <atomic>
#include <thread>
#include <cstdint>

namespace replication {

/// Receives journal entries from a primary site and persists to local journal.
///
/// Runs a TCP server on the replication port. For each received entry:
///   1. Verify CRC32
///   2. Verify sequence continuity (expected = last_received + 1)
///   3. Append to local IJournal
///   4. Send ack byte (0x06)
class JournalReceiver {
public:
    struct Config {
        uint16_t listen_port = 9878;
    };

    JournalReceiver(const Config& config, engine::IJournal* journal);
    ~JournalReceiver();

    // Non-copyable
    JournalReceiver(const JournalReceiver&) = delete;
    JournalReceiver& operator=(const JournalReceiver&) = delete;

    /// Start the receiver (blocking -- call from dedicated thread).
    bool start();

    /// Stop the receiver.
    void stop();

    /// Get statistics.
    struct ReceiverStats {
        uint64_t entries_received = 0;
        uint64_t entries_persisted = 0;
        uint64_t crc_failures = 0;
        uint64_t sequence_gaps = 0;
        engine::SequenceNum last_received_seq = 0;
        bool client_connected = false;
    };
    ReceiverStats stats() const;

    bool is_running() const { return running_.load(); }

    /// Get the actual bound port (useful when configured with port 0).
    uint16_t bound_port() const { return bound_port_; }

private:
    Config config_;
    engine::IJournal* journal_;
    std::atomic<bool> running_{false};
    ReceiverStats stats_;
    engine::SequenceNum expected_seq_ = 1;
    int listen_fd_ = -1;
    uint16_t bound_port_ = 0;

    static constexpr size_t HEADER_SIZE = 24;
    static constexpr size_t CRC_SIZE = 4;
    static constexpr uint8_t ACK_BYTE = 0x06;
    struct RecordHeader {
        uint64_t sequence;
        uint64_t timestamp;
        uint16_t partition_id;
        uint16_t entry_type;
        uint32_t payload_size;
    };

    void handle_client(int client_fd);
    bool recv_all(int fd, void* buf, size_t len);
    static uint32_t compute_crc32(const void* data, size_t len);
};

}  // namespace replication
