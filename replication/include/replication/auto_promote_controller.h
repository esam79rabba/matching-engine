#pragma once
#include "replication/health_probe.h"
#include "replication/failover_controller.h"
#include "replication/generation_counter.h"
#include "replication/journal_receiver.h"
#include "engine/journal.h"
#include "engine/partition.h"
#include "engine/matching.h"
#include <atomic>
#include <thread>
#include <functional>
#include <string>
#include <vector>
#include <mutex>

namespace replication {

/// Automated failover promotion controller.
///
/// Orchestrates the full sequence:
///   1. HealthProbe detects FAILED state
///   2. Validate standby readiness (lag=0, entries>0)
///   3. Fence old primary (increment generation)
///   4. Promote standby (cold-start from replicated journal)
///   5. Audit log all decisions
///
/// Safety:
///   - Auto-promote can be disabled (enabled_ flag)
///   - Requires all validation checks to pass
///   - Generation counter prevents split-brain
///   - Every decision is logged
struct AutoPromoteConfig {
    bool enabled = true;                    // Master switch
    int promote_delay_ms = 5000;            // Wait after FAILED before promoting (extra safety)
    int min_receiver_entries = 1;           // Minimum entries in standby journal
};

struct PromoteDecision {
    bool promoted = false;
    bool attempted = false;
    std::string reason;
    uint64_t new_generation = 0;
    uint64_t orders_restored = 0;
    uint64_t entries_replayed = 0;
};

class AutoPromoteController {
public:
    AutoPromoteController(const AutoPromoteConfig& config,
                          HealthProbe* probe,
                          FailoverController* failover_ctrl,
                          GenerationCounter* generation,
                          JournalReceiver* receiver);

    /// Start monitoring (background thread). Watches probe state
    /// and auto-promotes when conditions are met.
    void start();
    void stop();

    /// Set the partition and instruments for promotion.
    /// Must be called before start().
    void set_promotion_target(engine::IJournal* dr_journal,
                              engine::Partition* partition,
                              const std::vector<engine::InstrumentConfig>& instruments);

    /// Set audit callback (called on every decision).
    using AuditCallback = std::function<void(const std::string& action, const std::string& details)>;
    void set_audit_callback(AuditCallback cb) { audit_cb_ = std::move(cb); }

    /// Set metrics callback (called on promote).
    using MetricsCallback = std::function<void(const std::string& metric)>;
    void set_metrics_callback(MetricsCallback cb) { metrics_cb_ = std::move(cb); }

    /// Manual override: force enable/disable auto-promote.
    void set_enabled(bool enabled) { config_.enabled = enabled; }
    bool is_enabled() const { return config_.enabled; }

    /// Get the last promotion decision.
    PromoteDecision last_decision() const;

    bool is_running() const { return running_.load(); }

private:
    AutoPromoteConfig config_;
    HealthProbe* probe_;
    FailoverController* failover_ctrl_;
    GenerationCounter* generation_;
    JournalReceiver* receiver_;

    engine::IJournal* dr_journal_ = nullptr;
    engine::Partition* partition_ = nullptr;
    std::vector<engine::InstrumentConfig> instruments_;

    std::atomic<bool> running_{false};
    std::thread monitor_thread_;
    mutable std::mutex decision_mutex_;
    PromoteDecision last_decision_;

    AuditCallback audit_cb_;
    MetricsCallback metrics_cb_;

    void monitor_loop();
    PromoteDecision attempt_promote();
    void audit(const std::string& action, const std::string& details);
    void metric(const std::string& name);
};

}  // namespace replication
