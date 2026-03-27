#pragma once

#include "engine/types.h"

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace ops {

/// Lightweight WebSocket server for real-time push notifications.
///
/// Supports channels: trades, depth, status, alerts, metrics.
/// Clients connect via ws://host:port and send subscribe/unsubscribe JSON messages.
/// Server pushes events as JSON frames.
///
/// Protocol: RFC 6455 WebSocket over plain TCP.
/// Frame format: text frames only (opcode 0x1), no fragmentation.
class WebSocketServer {
public:
    explicit WebSocketServer(uint16_t port);
    ~WebSocketServer();

    /// Start accepting WebSocket connections (blocking).
    bool start();

    /// Stop the server and close all connections.
    void stop();

    /// Broadcast a message to all clients subscribed to a channel.
    void broadcast(const std::string& channel, const std::string& json_message);

    /// Send to a specific client.
    void send_to(int client_fd, const std::string& message);

    [[nodiscard]] bool is_running() const { return running_.load(); }
    [[nodiscard]] uint16_t port() const { return port_; }
    [[nodiscard]] uint32_t client_count() const;

    /// Channel names
    static constexpr const char* CH_TRADES  = "trades";
    static constexpr const char* CH_DEPTH   = "depth";
    static constexpr const char* CH_STATUS  = "status";
    static constexpr const char* CH_ALERTS  = "alerts";
    static constexpr const char* CH_METRICS = "metrics";

private:
    uint16_t port_;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};

    // Client tracking
    mutable std::mutex clients_mutex_;
    std::unordered_set<int> client_fds_;
    std::unordered_map<int, std::unordered_set<std::string>> subscriptions_;

    // Active client threads
    std::atomic<uint32_t> active_clients_{0};

    void handle_client(int client_fd);
    bool perform_handshake(int client_fd, const std::string& request);
    void process_frame(int client_fd, const std::string& payload);
    void remove_client(int client_fd);

    // WebSocket frame encoding/decoding
    static std::string encode_frame(const std::string& payload);
    static std::string decode_frame(const uint8_t* data, size_t len, size_t& consumed);
    static std::string compute_accept_key(const std::string& client_key);
};

}  // namespace ops
