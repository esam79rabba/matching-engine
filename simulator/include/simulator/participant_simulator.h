#pragma once

/// Participant Simulator — realistic FIX 4.4 market-making client.
///
/// Connects to a venue over TCP (or TLS) and exercises the full
/// order lifecycle: Logon, NewOrderSingle, OrderCancelRequest, Logout.
/// Designed to validate external integration against the matching engine.

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <cstdint>
#include <mutex>

namespace simulator {

struct SimulatorConfig {
    std::string host = "127.0.0.1";
    uint16_t port = 9876;
    bool use_tls = false;
    std::string sender_comp_id = "SIM_MM";
    std::string target_comp_id = "VENUE";
    uint32_t heartbeat_sec = 30;

    // Trading behavior
    std::string symbol = "BTC-USD";
    int64_t base_price = 50000;
    int64_t spread = 100;          // Half-spread from base
    uint64_t order_qty = 10;
    int orders_per_second = 2;     // Rate of order submission
    int duration_seconds = 60;     // How long to run
    bool cancel_stale = true;      // Cancel and replace orders
};

struct SimulatorStats {
    uint64_t orders_sent = 0;
    uint64_t acks_received = 0;
    uint64_t fills_received = 0;
    uint64_t rejects_received = 0;
    uint64_t cancels_sent = 0;
    uint64_t cancel_acks = 0;
    uint64_t errors = 0;
    uint64_t total_volume = 0;
    double avg_latency_us = 0;
};

/// Simulates a market-making participant.
///
/// Behavior:
///   1. Connects to venue (TCP or TLS)
///   2. Sends FIX Logon
///   3. Continuously:
///      a. Submits resting buy at base_price - spread
///      b. Submits resting sell at base_price + spread
///      c. Waits for fills or timeout
///      d. Cancels stale orders periodically
///      e. Replaces with new prices (simulating quote update)
///   4. Sends Logout on completion
///   5. Reports statistics
class ParticipantSimulator {
public:
    explicit ParticipantSimulator(const SimulatorConfig& config);
    ~ParticipantSimulator();

    // Non-copyable
    ParticipantSimulator(const ParticipantSimulator&) = delete;
    ParticipantSimulator& operator=(const ParticipantSimulator&) = delete;

    /// Run the full simulation (blocking).
    SimulatorStats run();

    /// Stop early.
    void stop();

    /// Get current stats (thread-safe snapshot).
    SimulatorStats stats() const;

    /// Set callback for each received execution report.
    using ExecCallback = std::function<void(const std::string& exec_type, const std::string& details)>;
    void set_exec_callback(ExecCallback cb) { exec_cb_ = std::move(cb); }

private:
    SimulatorConfig config_;
    std::atomic<bool> running_{false};
    mutable std::mutex stats_mutex_;
    SimulatorStats stats_;
    ExecCallback exec_cb_;

    int socket_fd_ = -1;
    void* ssl_ = nullptr;      // SSL* if TLS
    void* ssl_ctx_ = nullptr;  // SSL_CTX* if TLS
    uint64_t msg_seq_ = 1;
    uint64_t cl_ord_id_ = 1;

    // Track outstanding orders for cancel
    struct OutstandingOrder {
        std::string cl_ord_id;
        std::string venue_order_id;
        std::string side;
    };
    std::vector<OutstandingOrder> outstanding_;

    // Connection
    bool connect();
    void disconnect();
    bool send_fix(const std::string& msg);
    std::string recv_fix();

    // FIX message building
    std::string build_logon();
    std::string build_logout();
    std::string build_new_order(const std::string& cl_ord_id, const std::string& side,
                                 int64_t price, uint64_t qty);
    std::string build_cancel(const std::string& cl_ord_id, const std::string& orig_order_id,
                              const std::string& side);
    std::string build_heartbeat();

    // FIX message framing
    std::string build_fix_message(const std::string& body) const;
    static std::string calculate_checksum(const std::string& message);

    // FIX message parsing
    static std::string extract_tag(const std::string& msg, int tag);

    // Trading loop
    void trading_loop();
    void process_response(const std::string& response);

    // Helpers
    std::string next_cl_ord_id();
    std::string timestamp() const;
};

}  // namespace simulator
