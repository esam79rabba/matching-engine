#pragma once

#include "marketdata/publisher.h"
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <fcntl.h>

namespace marketdata {

/// TCP-based market data server — prototype transport.
///
/// Clients connect via TCP and receive JSON-encoded market data events.
/// Each event is a newline-delimited JSON object.
///
/// This is a prototype for integration testing. Production transport:
///   - Multicast UDP for colocated participants (SBE encoding)
///   - TCP/WebSocket for WAN participants (Phase 2)
///
/// Features:
///   - Accepts multiple client connections
///   - Broadcasts events to all connected clients
///   - Tracks sequence numbers for gap detection
///   - Snapshot hook: clients can request current state on connect
///
/// Deferred:
///   - SBE binary encoding (uses JSON for prototype)
///   - Multicast UDP
///   - Gap recovery protocol
///   - Conflation for slow consumers
///   - TLS
class MdTcpServer : public IPublisherSink {
public:
    explicit MdTcpServer(uint16_t port);
    ~MdTcpServer();

    /// Start the server (non-blocking — creates accept thread).
    bool start();

    /// Stop the server.
    void stop();

    /// IPublisherSink implementation — broadcasts to all connected clients.
    void publish_top_of_book(const TopOfBook& tob) override;
    void publish_depth_update(const DepthUpdate& update) override;
    void publish_trade(const TradeMessage& trade) override;
    void publish_instrument_status(const InstrumentStatus& status) override;

    [[nodiscard]] uint32_t client_count() const;
    [[nodiscard]] uint64_t messages_sent() const { return messages_sent_.load(); }
    [[nodiscard]] uint64_t messages_dropped() const { return messages_dropped_.load(); }
    [[nodiscard]] bool is_running() const { return running_.load(); }

private:
    uint16_t port_;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> messages_sent_{0};
    std::atomic<uint64_t> messages_dropped_{0};
    uint64_t sequence_ = 0;

    mutable std::mutex clients_mutex_;
    std::vector<int> client_fds_;

    void broadcast(const std::string& json_line);
    void accept_clients();
};

}  // namespace marketdata
