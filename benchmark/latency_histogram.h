#pragma once

/// HdrHistogram-style percentile tracking for nanosecond-precision latency measurement.
///
/// Uses a logarithmic bucket array with three tiers:
///   - Sub-microsecond: 1ns buckets (0–999ns)      = 1000 buckets
///   - Microsecond:     1µs buckets (1µs–999µs)    = 999 buckets
///   - Millisecond:     1ms buckets (1ms–999ms)     = 999 buckets
///
/// Total: 2998 buckets covering 0ns to ~1s with precision appropriate to each range.
/// No external dependencies — self-contained, header-only.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <iomanip>

namespace benchmark {

class LatencyHistogram {
public:
    /// Construct a histogram.
    /// max_value_ns: maximum recordable value (clamped to internal max of ~1s)
    /// significant_digits: unused (API compatibility with HdrHistogram)
    explicit LatencyHistogram(int64_t max_value_ns = 1000000000, int /*significant_digits*/ = 3)
        : max_value_ns_(std::min(max_value_ns, static_cast<int64_t>(999999999)))
        , count_(0)
        , total_ns_(0)
        , min_ns_(std::numeric_limits<int64_t>::max())
        , max_ns_(0) {
        std::memset(ns_buckets_, 0, sizeof(ns_buckets_));
        std::memset(us_buckets_, 0, sizeof(us_buckets_));
        std::memset(ms_buckets_, 0, sizeof(ms_buckets_));
    }

    /// Record a latency value in nanoseconds.
    void record(int64_t value_ns) noexcept {
        if (value_ns < 0) value_ns = 0;

        ++count_;
        total_ns_ += value_ns;
        if (value_ns < min_ns_) min_ns_ = value_ns;
        if (value_ns > max_ns_) max_ns_ = value_ns;

        if (value_ns < 1000) {
            // Sub-microsecond: 1ns precision (bucket 0–999)
            ns_buckets_[value_ns]++;
        } else if (value_ns < 1000000) {
            // Microsecond range: 1µs precision (bucket 0–998)
            int idx = static_cast<int>(value_ns / 1000) - 1;
            us_buckets_[idx]++;
        } else if (value_ns < 1000000000) {
            // Millisecond range: 1ms precision (bucket 0–998)
            int idx = static_cast<int>(value_ns / 1000000) - 1;
            ms_buckets_[idx]++;
        } else {
            // Overflow: clamp to last ms bucket
            ms_buckets_[998]++;
        }
    }

    /// Return the value at the given percentile (0.0–100.0).
    [[nodiscard]] int64_t percentile(double p) const noexcept {
        if (count_ == 0) return 0;

        // Target count for this percentile
        int64_t target = static_cast<int64_t>(std::ceil((p / 100.0) * static_cast<double>(count_)));
        if (target < 1) target = 1;
        if (target > count_) target = count_;

        int64_t cumulative = 0;

        // Scan ns buckets (0–999ns)
        for (int i = 0; i < 1000; ++i) {
            cumulative += ns_buckets_[i];
            if (cumulative >= target) return i;
        }

        // Scan us buckets (1µs–999µs)
        for (int i = 0; i < 999; ++i) {
            cumulative += us_buckets_[i];
            if (cumulative >= target) return static_cast<int64_t>(i + 1) * 1000;
        }

        // Scan ms buckets (1ms–999ms)
        for (int i = 0; i < 999; ++i) {
            cumulative += ms_buckets_[i];
            if (cumulative >= target) return static_cast<int64_t>(i + 1) * 1000000;
        }

        return max_ns_;
    }

    [[nodiscard]] int64_t min() const noexcept { return count_ == 0 ? 0 : min_ns_; }
    [[nodiscard]] int64_t max() const noexcept { return max_ns_; }
    [[nodiscard]] double mean() const noexcept { return count_ == 0 ? 0.0 : static_cast<double>(total_ns_) / static_cast<double>(count_); }
    [[nodiscard]] int64_t count() const noexcept { return count_; }

    void reset() noexcept {
        count_ = 0;
        total_ns_ = 0;
        min_ns_ = std::numeric_limits<int64_t>::max();
        max_ns_ = 0;
        std::memset(ns_buckets_, 0, sizeof(ns_buckets_));
        std::memset(us_buckets_, 0, sizeof(us_buckets_));
        std::memset(ms_buckets_, 0, sizeof(ms_buckets_));
    }

    /// Format a nanosecond value with appropriate unit.
    static std::string format_ns(int64_t ns) {
        std::ostringstream oss;
        if (ns < 1000) {
            oss << ns << " ns";
        } else if (ns < 1000000) {
            oss << std::fixed << std::setprecision(0) << (ns / 1000.0) << " us";
        } else if (ns < 1000000000) {
            oss << std::fixed << std::setprecision(2) << (ns / 1000000.0) << " ms";
        } else {
            oss << std::fixed << std::setprecision(3) << (ns / 1000000000.0) << " s";
        }
        return oss.str();
    }

    /// Human-readable summary of the histogram.
    [[nodiscard]] std::string summary() const {
        std::ostringstream oss;
        oss << "  Latency (nanoseconds):\n"
            << "    p50:         " << std::setw(12) << format_ns(percentile(50.0)) << "\n"
            << "    p99:         " << std::setw(12) << format_ns(percentile(99.0)) << "\n"
            << "    p99.9:       " << std::setw(12) << format_ns(percentile(99.9)) << "\n"
            << "    p99.99:      " << std::setw(12) << format_ns(percentile(99.99)) << "\n"
            << "    worst:       " << std::setw(12) << format_ns(max_ns_) << "\n"
            << "    mean:        " << std::setw(12) << format_ns(static_cast<int64_t>(mean())) << "\n";
        return oss.str();
    }

private:
    int64_t max_value_ns_;
    int64_t count_;
    int64_t total_ns_;
    int64_t min_ns_;
    int64_t max_ns_;

    // Tier 1: 0–999 ns (1ns buckets)
    int64_t ns_buckets_[1000];

    // Tier 2: 1µs–999µs (1µs buckets), index 0 = 1µs range
    int64_t us_buckets_[999];

    // Tier 3: 1ms–999ms (1ms buckets), index 0 = 1ms range
    int64_t ms_buckets_[999];
};

}  // namespace benchmark
