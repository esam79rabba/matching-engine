#include "ops/metrics.h"
#include <sstream>
#include <algorithm>
#include <vector>

namespace ops {

Metrics& Metrics::instance() {
    static Metrics inst;
    return inst;
}

void Metrics::increment(const std::string& name, uint64_t amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_[name] += amount;
}

void Metrics::set_gauge(const std::string& name, int64_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    gauges_[name] = value;
}

uint64_t Metrics::get_counter(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = counters_.find(name);
    return (it != counters_.end()) ? it->second : 0;
}

int64_t Metrics::get_gauge(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = gauges_.find(name);
    return (it != gauges_.end()) ? it->second : 0;
}

/// Replace dots with underscores for Prometheus-compatible metric names.
static std::string prometheus_name(const std::string& name) {
    std::string result = name;
    std::replace(result.begin(), result.end(), '.', '_');
    return result;
}

std::string Metrics::to_prometheus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;

    // Sort keys for deterministic output
    std::vector<std::string> counter_keys;
    counter_keys.reserve(counters_.size());
    for (const auto& [k, v] : counters_) counter_keys.push_back(k);
    std::sort(counter_keys.begin(), counter_keys.end());

    for (const auto& name : counter_keys) {
        auto pname = prometheus_name(name);
        oss << "# TYPE " << pname << " counter\n"
            << pname << " " << counters_.at(name) << "\n";
    }

    std::vector<std::string> gauge_keys;
    gauge_keys.reserve(gauges_.size());
    for (const auto& [k, v] : gauges_) gauge_keys.push_back(k);
    std::sort(gauge_keys.begin(), gauge_keys.end());

    for (const auto& name : gauge_keys) {
        auto pname = prometheus_name(name);
        oss << "# TYPE " << pname << " gauge\n"
            << pname << " " << gauges_.at(name) << "\n";
    }

    return oss.str();
}

std::string Metrics::to_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    oss << "{\"counters\":{";

    bool first = true;
    for (const auto& [name, value] : counters_) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << name << "\":" << value;
    }

    oss << "},\"gauges\":{";

    first = true;
    for (const auto& [name, value] : gauges_) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << name << "\":" << value;
    }

    oss << "}}";
    return oss.str();
}

void Metrics::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_.clear();
    gauges_.clear();
}

}  // namespace ops
