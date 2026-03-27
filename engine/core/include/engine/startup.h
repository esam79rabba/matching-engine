#pragma once

#include "engine/partition.h"
#include "engine/snapshot.h"
#include "engine/journal.h"
#include "engine/matching.h"

#include <string>
#include <cstring>

namespace engine {

/// Startup/warm-restart controller.
///
/// Orchestrates the startup sequence:
///   1. Load latest snapshot (if available)
///   2. Restore partition state from snapshot
///   3. Replay journal entries from snapshot's sequence point
///   4. Validate restored state
///   5. Mark partition ready for new traffic
///
/// This is the warm-restart path from the frozen baseline (ADR-005).
/// DR failover (replaying from a remote journal) is Phase 2+.
///
/// Generic — no asset-class-specific logic.
struct StartupResult {
    bool     success = false;
    uint64_t snapshot_orders_restored = 0;
    uint64_t journal_entries_replayed = 0;
    SequenceNum final_sequence = 0;
    std::string error;
};

class StartupController {
public:
    /// Perform warm restart: snapshot load + journal replay.
    /// The partition must already have instruments registered (from ref data).
    /// The journal provides entries to replay from the snapshot point.
    static StartupResult warm_restart(Partition& partition,
                                       const Snapshot& snap,
                                       const IJournal& journal);

    /// Cold start: replay entire journal from sequence 1 (no snapshot).
    static StartupResult cold_start(Partition& partition,
                                     const IJournal& journal);

    /// Validate partition state after restore.
    /// Returns true if state appears consistent.
    static bool validate_state(const Partition& partition);
};

}  // namespace engine
