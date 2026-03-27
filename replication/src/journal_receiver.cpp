#include "replication/journal_receiver.h"

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

static uint32_t crc32_table_recv[256];
static bool crc32_initialized_recv = false;

static void init_crc32_table_recv() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0);
        }
        crc32_table_recv[i] = crc;
    }
    crc32_initialized_recv = true;
}

uint32_t JournalReceiver::compute_crc32(const void* data, size_t len) {
    if (!crc32_initialized_recv) init_crc32_table_recv();

    auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc = crc32_table_recv[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

// ─── Constructor / Destructor ────────────────────────────────

JournalReceiver::JournalReceiver(const Config& config, engine::IJournal* journal)
    : config_(config), journal_(journal) {}

JournalReceiver::~JournalReceiver() {
    stop();
}

// ─── Start / Stop ────────────────────────────────────────────

bool JournalReceiver::start() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;

    // Allow port reuse
    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(config_.listen_port);

    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    // Retrieve the actual bound port (important when listen_port == 0)
    struct sockaddr_in bound_addr{};
    socklen_t addr_len = sizeof(bound_addr);
    if (::getsockname(listen_fd_, reinterpret_cast<struct sockaddr*>(&bound_addr), &addr_len) == 0) {
        bound_port_ = ntohs(bound_addr.sin_port);
    }

    if (::listen(listen_fd_, 1) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    running_.store(true);

    // Accept loop with poll timeout so we can check running_ flag
    while (running_.load()) {
        struct pollfd pfd{};
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;

        int ret = ::poll(&pfd, 1, 200);  // 200ms timeout
        if (ret <= 0) continue;

        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(listen_fd_,
                                  reinterpret_cast<struct sockaddr*>(&client_addr),
                                  &client_len);
        if (client_fd < 0) continue;

        // Set TCP_NODELAY on client socket
        int flag = 1;
        ::setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        stats_.client_connected = true;
        handle_client(client_fd);
        stats_.client_connected = false;
    }

    return true;
}

void JournalReceiver::stop() {
    running_.store(false);
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

// ─── Stats ───────────────────────────────────────────────────

JournalReceiver::ReceiverStats JournalReceiver::stats() const {
    return stats_;
}

// ─── Client handling ─────────────────────────────────────────

void JournalReceiver::handle_client(int client_fd) {
    while (running_.load()) {
        // Check if data is available (with timeout so we can check running_)
        struct pollfd pfd{};
        pfd.fd = client_fd;
        pfd.events = POLLIN;

        int ret = ::poll(&pfd, 1, 200);
        if (ret < 0) break;
        if (ret == 0) continue;  // Timeout, check running_ again
        if (pfd.revents & (POLLERR | POLLHUP)) break;

        // Read header
        RecordHeader hdr{};
        if (!recv_all(client_fd, &hdr, HEADER_SIZE)) break;

        // Read payload
        std::vector<uint8_t> payload(hdr.payload_size);
        if (hdr.payload_size > 0) {
            if (!recv_all(client_fd, payload.data(), hdr.payload_size)) break;
        }

        // Read CRC
        uint32_t received_crc = 0;
        if (!recv_all(client_fd, &received_crc, CRC_SIZE)) break;

        // Verify CRC
        std::vector<uint8_t> crc_buf(HEADER_SIZE + hdr.payload_size);
        std::memcpy(crc_buf.data(), &hdr, HEADER_SIZE);
        if (hdr.payload_size > 0) {
            std::memcpy(crc_buf.data() + HEADER_SIZE, payload.data(), hdr.payload_size);
        }
        uint32_t computed_crc = compute_crc32(crc_buf.data(), crc_buf.size());

        if (computed_crc != received_crc) {
            stats_.crc_failures++;
            // Send NACK (0x15)
            uint8_t nack = 0x15;
            ::send(client_fd, &nack, 1, 0);
            continue;
        }

        stats_.entries_received++;

        // Verify sequence continuity
        if (expected_seq_ != 0 && hdr.sequence != expected_seq_) {
            stats_.sequence_gaps++;
        }

        // Append to local journal
        engine::JournalEntry entry{};
        entry.sequence = hdr.sequence;
        entry.timestamp = hdr.timestamp;
        entry.partition_id = hdr.partition_id;
        entry.entry_type = hdr.entry_type;
        entry.payload_size = hdr.payload_size;
        entry.payload = payload.empty() ? nullptr : payload.data();

        auto result = journal_->append(entry);
        if (result == engine::JournalResult::OK) {
            stats_.entries_persisted++;
            stats_.last_received_seq = hdr.sequence;
            expected_seq_ = hdr.sequence + 1;
        }

        // Send ACK
        uint8_t ack = ACK_BYTE;
        ::send(client_fd, &ack, 1, 0);
    }

    ::close(client_fd);
}

bool JournalReceiver::recv_all(int fd, void* buf, size_t len) {
    auto* ptr = static_cast<uint8_t*>(buf);
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t n = ::recv(fd, ptr, remaining, 0);
        if (n <= 0) return false;
        ptr += n;
        remaining -= static_cast<size_t>(n);
    }
    return true;
}

}  // namespace replication
