#include "risk/rate_limiter.h"

namespace risk {

void RateLimiter::configure(engine::SessionId session_id, const Config& config) {
    configs_[session_id] = config;
}

bool RateLimiter::check_and_increment(engine::SessionId session_id,
                                       engine::Timestamp now_ns) {
    const auto& cfg = get_config(session_id);
    auto& state = states_[session_id];

    uint64_t window_ns = static_cast<uint64_t>(cfg.window_ms) * 1000000ULL;

    // Evict timestamps outside the sliding window
    evict_old(state, window_ns, now_ns);

    // Check limit
    if (static_cast<uint32_t>(state.timestamps.size()) >= cfg.max_messages_per_window) {
        return false;  // Rate limit exceeded
    }

    state.timestamps.push_back(now_ns);
    return true;  // Allowed
}

uint32_t RateLimiter::current_count(engine::SessionId session_id) const {
    auto it = states_.find(session_id);
    return (it != states_.end()) ? static_cast<uint32_t>(it->second.timestamps.size()) : 0;
}

void RateLimiter::reset(engine::SessionId session_id) {
    states_.erase(session_id);
}

const RateLimiter::Config& RateLimiter::get_config(engine::SessionId sid) const {
    auto it = configs_.find(sid);
    return (it != configs_.end()) ? it->second : defaults_;
}

void RateLimiter::evict_old(WindowState& state, uint64_t window_ns,
                             engine::Timestamp now_ns) const {
    while (!state.timestamps.empty()) {
        if (now_ns - state.timestamps.front() >= window_ns) {
            state.timestamps.pop_front();
        } else {
            break;
        }
    }
}

}  // namespace risk
