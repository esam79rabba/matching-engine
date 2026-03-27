#include "replication/auto_promote_controller.h"

#include <chrono>
#include <sstream>

namespace replication {

// ─── Constructor ─────────────────────────────────────────────

AutoPromoteController::AutoPromoteController(const AutoPromoteConfig& config,
                                             HealthProbe* probe,
                                             FailoverController* failover_ctrl,
                                             GenerationCounter* generation,
                                             JournalReceiver* receiver)
    : config_(config)
    , probe_(probe)
    , failover_ctrl_(failover_ctrl)
    , generation_(generation)
    , receiver_(receiver) {}

// ─── Start / Stop ────────────────────────────────────────────

void AutoPromoteController::start() {
    if (running_.load()) return;
    running_.store(true);
    monitor_thread_ = std::thread([this]() { monitor_loop(); });
}

void AutoPromoteController::stop() {
    running_.store(false);
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
}

// ─── Promotion target ────────────────────────────────────────

void AutoPromoteController::set_promotion_target(engine::IJournal* dr_journal,
                                                  engine::Partition* partition,
                                                  const std::vector<engine::InstrumentConfig>& instruments) {
    dr_journal_ = dr_journal;
    partition_ = partition;
    instruments_ = instruments;
}

// ─── Last decision ───────────────────────────────────────────

PromoteDecision AutoPromoteController::last_decision() const {
    std::lock_guard<std::mutex> lock(decision_mutex_);
    return last_decision_;
}

// ─── Monitor loop ────────────────────────────────────────────

void AutoPromoteController::monitor_loop() {
    while (running_.load()) {
        // Poll every 500ms, but check running_ more frequently for quick shutdown
        for (int i = 0; i < 10 && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!running_.load()) break;

        // Check if primary has failed
        if (probe_->state() != HealthProbe::State::FAILED) {
            continue;
        }

        // Primary is FAILED — begin failover sequence
        audit("failover_detection", "Primary health probe reports FAILED; consecutive_failures=" +
              std::to_string(probe_->consecutive_failures()));

        if (!config_.enabled) {
            audit("auto_promote_disabled", "Auto-promote is disabled; manual intervention required");
            continue;
        }

        // Safety delay: wait before promoting
        int remaining_ms = config_.promote_delay_ms;
        while (remaining_ms > 0 && running_.load()) {
            int sleep_ms = std::min(remaining_ms, 50);
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
            remaining_ms -= sleep_ms;
        }
        if (!running_.load()) break;

        // Re-check: primary may have recovered during the delay
        if (probe_->state() != HealthProbe::State::FAILED) {
            audit("failover_aborted", "Primary recovered during promote delay");
            continue;
        }

        // Attempt promotion
        PromoteDecision decision = attempt_promote();
        {
            std::lock_guard<std::mutex> lock(decision_mutex_);
            last_decision_ = decision;
        }

        if (decision.promoted) {
            audit("promote_success", "Promoted to primary; generation=" +
                  std::to_string(decision.new_generation) +
                  " orders_restored=" + std::to_string(decision.orders_restored) +
                  " entries_replayed=" + std::to_string(decision.entries_replayed));
            metric("failover.promotions");
            // Stop monitoring after successful promotion
            running_.store(false);
            break;
        } else {
            audit("promote_failed", "Promotion attempt failed: " + decision.reason);
        }
    }
}

// ─── Attempt promote ─────────────────────────────────────────

PromoteDecision AutoPromoteController::attempt_promote() {
    PromoteDecision decision;
    decision.attempted = true;

    // Check failover controller readiness
    auto status = failover_ctrl_->status();
    if (!status.ready_to_promote) {
        decision.reason = "not ready: " + status.not_ready_reason;
        return decision;
    }

    // Check receiver has minimum entries
    auto rstats = receiver_->stats();
    if (rstats.entries_received < static_cast<uint64_t>(config_.min_receiver_entries)) {
        decision.reason = "insufficient entries: received=" +
                          std::to_string(rstats.entries_received) +
                          " required=" + std::to_string(config_.min_receiver_entries);
        return decision;
    }

    // Fence: increment generation to prevent split-brain
    uint64_t new_gen = generation_->increment();
    decision.new_generation = new_gen;
    audit("fencing", "Generation incremented to " + std::to_string(new_gen));
    metric("failover.fencing_events");

    // Promote: cold-start from replicated journal
    if (!dr_journal_ || !partition_) {
        decision.reason = "promotion target not set (journal or partition is null)";
        return decision;
    }

    auto result = failover_ctrl_->promote(dr_journal_, *partition_, instruments_);
    if (result.success) {
        decision.promoted = true;
        decision.orders_restored = result.orders_restored;
        decision.entries_replayed = result.journal_entries_replayed;
    } else {
        decision.reason = "promote failed: " + result.error;
    }

    return decision;
}

// ─── Audit / Metrics ─────────────────────────────────────────

void AutoPromoteController::audit(const std::string& action, const std::string& details) {
    if (audit_cb_) {
        audit_cb_(action, details);
    }
}

void AutoPromoteController::metric(const std::string& name) {
    if (metrics_cb_) {
        metrics_cb_(name);
    }
}

}  // namespace replication
