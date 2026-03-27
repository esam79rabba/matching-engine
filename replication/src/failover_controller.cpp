#include "replication/failover_controller.h"

#include <sstream>

namespace replication {

// ─── Constructor ─────────────────────────────────────────────

FailoverController::FailoverController(HealthProbe* probe, JournalReceiver* receiver)
    : probe_(probe), receiver_(receiver) {}

// ─── Status ──────────────────────────────────────────────────

static std::string state_to_string(HealthProbe::State s) {
    switch (s) {
        case HealthProbe::State::HEALTHY:  return "healthy";
        case HealthProbe::State::DEGRADED: return "degraded";
        case HealthProbe::State::FAILED:   return "failed";
    }
    return "unknown";
}

FailoverStatus FailoverController::status() const {
    FailoverStatus st;

    // Probe state
    auto probe_state = probe_->state();
    st.probe_state = state_to_string(probe_state);
    st.consecutive_failures = probe_->consecutive_failures();

    // Receiver stats
    auto rstats = receiver_->stats();
    st.receiver_entries = rstats.entries_received;

    // Replication lag: we only know what the receiver has seen.
    // If there's a gap between persisted and received, that's our lag indicator.
    st.replication_lag = rstats.entries_received - rstats.entries_persisted;

    // Readiness assessment
    st.ready_to_promote = false;
    if (probe_state != HealthProbe::State::FAILED) {
        st.not_ready_reason = "Primary is still " + st.probe_state +
                              "; must be FAILED to promote";
    } else if (rstats.entries_received == 0) {
        st.not_ready_reason = "No journal entries received; nothing to recover from";
    } else if (st.replication_lag > 0) {
        st.not_ready_reason = "Replication lag is non-zero (" +
                              std::to_string(st.replication_lag) + " entries pending)";
    } else {
        st.ready_to_promote = true;
    }

    return st;
}

// ─── Promote ─────────────────────────────────────────────────

PromoteResult FailoverController::promote(engine::IJournal* dr_journal,
                                           engine::Partition& partition,
                                           const std::vector<engine::InstrumentConfig>& instruments) {
    PromoteResult result;

    // Pre-check: probe should be FAILED
    if (probe_->state() != HealthProbe::State::FAILED) {
        result.error = "Cannot promote: primary is still " +
                       state_to_string(probe_->state()) +
                       "; must be FAILED";
        return result;
    }

    // Register instruments on the new partition
    for (const auto& inst : instruments) {
        partition.add_instrument(inst);
    }

    // Cold-start from the replicated journal
    auto startup = engine::StartupController::cold_start(partition, *dr_journal);
    if (!startup.success) {
        result.error = "Cold start failed: " + startup.error;
        return result;
    }

    result.success = true;
    result.orders_restored = startup.snapshot_orders_restored;
    result.journal_entries_replayed = startup.journal_entries_replayed;
    return result;
}

// ─── JSON export ─────────────────────────────────────────────

std::string FailoverController::status_json() const {
    auto st = status();

    std::ostringstream oss;
    oss << "{"
        << "\"probe_state\":\"" << st.probe_state << "\","
        << "\"consecutive_failures\":" << st.consecutive_failures << ","
        << "\"replication_lag\":" << st.replication_lag << ","
        << "\"receiver_entries\":" << st.receiver_entries << ","
        << "\"ready_to_promote\":" << (st.ready_to_promote ? "true" : "false");
    if (!st.not_ready_reason.empty()) {
        oss << ",\"not_ready_reason\":\"" << st.not_ready_reason << "\"";
    }
    oss << "}";
    return oss.str();
}

}  // namespace replication
