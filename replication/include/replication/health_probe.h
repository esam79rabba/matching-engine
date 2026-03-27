#pragma once
#include <string>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

namespace replication {

/// Sends periodic heartbeat to primary and tracks responsiveness.
/// If primary stops responding, reports failure.
class HealthProbe {
public:
    struct Config {
        std::string primary_host = "127.0.0.1";
        uint16_t    primary_port = 18080;     // Ops HTTP port
        std::string health_path = "/api/v1/health";
        int         interval_ms = 1000;        // Probe interval
        int         timeout_ms = 2000;         // Single probe timeout
        int         failure_threshold = 3;     // Consecutive failures before FAILED
    };

    explicit HealthProbe(const Config& config);
    ~HealthProbe();

    /// Start probing (non-blocking — runs in background thread).
    void start();
    void stop();

    /// Current state
    enum class State { HEALTHY, DEGRADED, FAILED };
    State state() const { return state_.load(); }
    int consecutive_failures() const { return consecutive_failures_.load(); }
    bool is_running() const { return running_.load(); }

    /// Callback when state changes
    using StateCallback = std::function<void(State old_state, State new_state)>;
    void set_state_callback(StateCallback cb) { state_callback_ = std::move(cb); }

    /// Force state for testing purposes.
    void force_state(State s) { state_.store(s); }

private:
    Config config_;
    std::atomic<State> state_{State::HEALTHY};
    std::atomic<int> consecutive_failures_{0};
    std::atomic<bool> running_{false};
    std::thread probe_thread_;
    StateCallback state_callback_;

    void probe_loop();
    bool check_health();  // Returns true if primary responds
};

}  // namespace replication
