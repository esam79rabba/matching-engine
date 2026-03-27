#pragma once
#include "replication/health_probe.h"
#include "replication/journal_receiver.h"
#include "engine/journal.h"
#include "engine/partition.h"
#include "engine/startup.h"
#include "engine/matching.h"
#include <string>

namespace replication {

/// Failover readiness and manual promote controller.
///
/// Runs on the DR site. Monitors:
///   - Health probe state (HEALTHY/DEGRADED/FAILED)
///   - Replication lag (from receiver stats)
///   - Journal integrity
///
/// Exposes:
///   - Readiness assessment (can we safely promote?)
///   - Manual promote action (cold-start from replicated journal)
struct FailoverStatus {
    std::string probe_state;     // "healthy", "degraded", "failed"
    int consecutive_failures = 0;
    uint64_t replication_lag = 0; // entries_sent - entries_received (from receiver perspective)
    uint64_t receiver_entries = 0;
    bool ready_to_promote = false;
    std::string not_ready_reason;
};

struct PromoteResult {
    bool success = false;
    std::string error;
    uint64_t orders_restored = 0;
    uint64_t journal_entries_replayed = 0;
};

class FailoverController {
public:
    FailoverController(HealthProbe* probe, JournalReceiver* receiver);

    /// Get current failover readiness status.
    FailoverStatus status() const;

    /// Promote this DR site to primary.
    /// Creates a new partition, cold-starts from the receiver's journal.
    /// Returns result with restored state details.
    ///
    /// Prerequisites:
    ///   - probe_state should be FAILED (primary confirmed down)
    ///   - replication_lag should be 0 (all entries received)
    ///   - Caller is responsible for fencing the old primary
    PromoteResult promote(engine::IJournal* dr_journal,
                          engine::Partition& partition,
                          const std::vector<engine::InstrumentConfig>& instruments);

    /// Export status as JSON string.
    std::string status_json() const;

private:
    HealthProbe* probe_;
    JournalReceiver* receiver_;
};

}  // namespace replication
