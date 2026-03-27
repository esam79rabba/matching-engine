#pragma once

#include "engine/types.h"
#include <deque>
#include <unordered_map>
#include <cstdint>

namespace risk {

/// Sliding-window rate limiter.
///
/// Tracks message counts per session within a configurable time window.
/// Uses a true sliding window (deque of timestamps) for smooth rate limiting
/// without burst artifacts at window boundaries.
///
/// Generic — no asset-class-specific logic.
class RateLimiter {
public:
    struct Config {
        uint32_t max_messages_per_window = 1000;
        uint32_t window_ms = 1000;  // Window size in milliseconds
    };

    /// Set limits for a specific session.
    void configure(engine::SessionId session_id, const Config& config);

    /// Set default limits for unconfigured sessions.
    void set_defaults(const Config& config) { defaults_ = config; }

    /// Check if a message is allowed. Returns true if within limit.
    /// Caller provides current timestamp (nanoseconds since epoch).
    /// If rejected, the caller should emit RejectCode::RateLimitExceeded.
    [[nodiscard]] bool check_and_increment(engine::SessionId session_id,
                                            engine::Timestamp now_ns);

    /// Query current rate for a session (messages in current window).
    [[nodiscard]] uint32_t current_count(engine::SessionId session_id) const;

    /// Reset counters for a session (e.g., on session logout).
    void reset(engine::SessionId session_id);

private:
    struct WindowState {
        std::deque<engine::Timestamp> timestamps;
    };

    Config defaults_;
    std::unordered_map<engine::SessionId, Config> configs_;
    std::unordered_map<engine::SessionId, WindowState> states_;

    [[nodiscard]] const Config& get_config(engine::SessionId sid) const;

    /// Evict timestamps older than window_ns before now_ns.
    void evict_old(WindowState& state, uint64_t window_ns, engine::Timestamp now_ns) const;
};

}  // namespace risk
