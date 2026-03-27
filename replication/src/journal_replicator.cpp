#include "replication/journal_replicator.h"

#include <cstring>
#include <vector>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

namespace replication {

// ─── CRC32 (same table-based implementation as FileJournal) ──

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

uint32_t JournalReplicator::compute_crc32(const void* data, size_t len) {
    if (!crc32_initialized) init_crc32_table();

    auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc = crc32_table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

// ─── Constructor / Destructor ────────────────────────────────

JournalReplicator::JournalReplicator(const Config& config)
    : config_(config) {}

JournalReplicator::~JournalReplicator() {
    disconnect();
}

// ─── Connect / Disconnect ────────────────────────────────────

bool JournalReplicator::connect() {
    if (connected_.load()) return true;

    socket_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) return false;

    // Set TCP_NODELAY for low-latency replication
    int flag = 1;
    ::setsockopt(socket_fd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // Set send/recv timeouts
    struct timeval tv{};
    tv.tv_sec = config_.ack_timeout_ms / 1000;
    tv.tv_usec = (config_.ack_timeout_ms % 1000) * 1000;
    ::setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.dr_port);
    ::inet_pton(AF_INET, config_.dr_host.c_str(), &addr.sin_addr);

    if (::connect(socket_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    connected_.store(true);
    stats_.connected = true;
    return true;
}

void JournalReplicator::disconnect() {
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
    connected_.store(false);
    stats_.connected = false;
}

// ─── Replicate ───────────────────────────────────────────────

bool JournalReplicator::replicate(const engine::JournalEntry& entry) {
    if (!connected_.load()) return false;

    // Build header
    RecordHeader hdr{};
    hdr.sequence = entry.sequence;
    hdr.timestamp = entry.timestamp;
    hdr.partition_id = entry.partition_id;
    hdr.entry_type = entry.entry_type;
    hdr.payload_size = entry.payload_size;

    // Compute CRC32 over header + payload
    std::vector<uint8_t> crc_buf(HEADER_SIZE + entry.payload_size);
    std::memcpy(crc_buf.data(), &hdr, HEADER_SIZE);
    if (entry.payload_size > 0 && entry.payload != nullptr) {
        std::memcpy(crc_buf.data() + HEADER_SIZE, entry.payload, entry.payload_size);
    }
    uint32_t crc = compute_crc32(crc_buf.data(), crc_buf.size());

    // Send: header + payload + CRC
    if (!send_all(&hdr, HEADER_SIZE)) {
        connected_.store(false);
        stats_.connected = false;
        return false;
    }
    if (entry.payload_size > 0 && entry.payload != nullptr) {
        if (!send_all(entry.payload, entry.payload_size)) {
            connected_.store(false);
            stats_.connected = false;
            return false;
        }
    }
    if (!send_all(&crc, CRC_SIZE)) {
        connected_.store(false);
        stats_.connected = false;
        return false;
    }

    uint64_t bytes = HEADER_SIZE + entry.payload_size + CRC_SIZE;
    stats_.entries_sent++;
    stats_.bytes_sent += bytes;

    // In SYNC mode, wait for ack
    if (config_.mode == ReplicationMode::SYNC) {
        if (!recv_ack()) {
            stats_.ack_failures++;
            connected_.store(false);
            stats_.connected = false;
            return false;
        }
        stats_.entries_acked++;
        stats_.last_acked_seq = entry.sequence;
    } else {
        // ASYNC: count as acked immediately (best-effort)
        stats_.entries_acked++;
        stats_.last_acked_seq = entry.sequence;
    }

    return true;
}

// ─── Stats ───────────────────────────────────────────────────

ReplicationStats JournalReplicator::stats() const {
    ReplicationStats s = stats_;
    s.connected = connected_.load();
    return s;
}

// ─── Private helpers ─────────────────────────────────────────

bool JournalReplicator::send_all(const void* data, size_t len) {
    auto* ptr = static_cast<const uint8_t*>(data);
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t sent = ::send(socket_fd_, ptr, remaining, 0);
        if (sent <= 0) return false;
        ptr += sent;
        remaining -= static_cast<size_t>(sent);
    }
    return true;
}

bool JournalReplicator::recv_ack() {
    uint8_t ack = 0;
    ssize_t n = ::recv(socket_fd_, &ack, 1, 0);
    if (n != 1) return false;
    return ack == 0x06;  // ACK byte
}

}  // namespace replication
