#pragma once
#include <string>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <cstdint>

namespace ops {

/// Lightweight metrics collector for pilot observability.
/// Thread-safe counters and gauges exposed in Prometheus text format.
///
/// Usage:
///   Metrics::instance().increment("fix.orders_accepted");
///   Metrics::instance().increment("fix.orders_rejected");
///   Metrics::instance().set_gauge("fix.session_count", 3);
///   std::string output = Metrics::instance().to_prometheus();
///
/// Prometheus text format:
///   # TYPE fix_orders_accepted counter
///   fix_orders_accepted 42
///
/// Not a production metrics system — sufficient for pilot monitoring.
class Metrics {
public:
    /// Get the global singleton.
    static Metrics& instance();

    /// Increment a counter by 1 (or by amount).
    void increment(const std::string& name, uint64_t amount = 1);

    /// Set a gauge value (can go up or down).
    void set_gauge(const std::string& name, int64_t value);

    /// Get a counter value.
    uint64_t get_counter(const std::string& name) const;

    /// Get a gauge value.
    int64_t get_gauge(const std::string& name) const;

    /// Export all metrics in Prometheus text exposition format.
    std::string to_prometheus() const;

    /// Export all metrics as JSON.
    std::string to_json() const;

    /// Reset all metrics (for testing).
    void reset();

private:
    Metrics() = default;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, uint64_t> counters_;
    std::unordered_map<std::string, int64_t> gauges_;
};

}  // namespace ops
