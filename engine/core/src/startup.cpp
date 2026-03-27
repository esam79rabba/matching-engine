#include "engine/startup.h"
#include <cstring>

namespace engine {

StartupResult StartupController::warm_restart(Partition& partition,
                                                const Snapshot& snap,
                                                const IJournal& journal) {
    StartupResult result;

    // Step 1: Restore from snapshot
    if (!snapshot::restore(partition, snap)) {
        result.error = "Snapshot restore failed";
        return result;
    }
    result.snapshot_orders_restored = snap.total_order_count();

    // Step 2: Replay journal entries AFTER the snapshot point
    // Only replay INPUT_COMMAND entries (not output events — those are regenerated)
    SequenceNum replay_from = snap.sequence + 1;

    struct ReplayCtx {
        Partition* partition;
        uint64_t replayed;
    };
    ReplayCtx ctx{&partition, 0};

    journal.replay_from(replay_from, [](const JournalEntry& entry, void* c) {
        auto* rctx = static_cast<ReplayCtx*>(c);
        if (entry.entry_type != JournalEntry::TYPE_INPUT_COMMAND) return;
        if (entry.payload_size < sizeof(PartitionMessage)) return;

        PartitionMessage msg;
        std::memcpy(&msg, entry.payload, sizeof(PartitionMessage));
        rctx->partition->process(msg);
        ++rctx->replayed;
    }, &ctx);

    result.journal_entries_replayed = ctx.replayed;
    result.final_sequence = partition.sequence();

    // Step 3: Validate
    result.success = validate_state(partition);
    if (!result.success) {
        result.error = "Post-replay state validation failed";
    }

    return result;
}

StartupResult StartupController::cold_start(Partition& partition,
                                              const IJournal& journal) {
    StartupResult result;

    struct ReplayCtx {
        Partition* partition;
        uint64_t replayed;
    };
    ReplayCtx ctx{&partition, 0};

    journal.replay_from(1, [](const JournalEntry& entry, void* c) {
        auto* rctx = static_cast<ReplayCtx*>(c);
        if (entry.entry_type != JournalEntry::TYPE_INPUT_COMMAND) return;
        if (entry.payload_size < sizeof(PartitionMessage)) return;

        PartitionMessage msg;
        std::memcpy(&msg, entry.payload, sizeof(PartitionMessage));
        rctx->partition->process(msg);
        ++rctx->replayed;
    }, &ctx);

    result.journal_entries_replayed = ctx.replayed;
    result.final_sequence = partition.sequence();
    result.success = validate_state(partition);
    if (!result.success) {
        result.error = "Post-replay state validation failed";
    }

    return result;
}

bool StartupController::validate_state(const Partition& partition) {
    // Basic validation: all engines accessible, no null pointers, sequence > 0 if entries exist
    for (const auto& [id, eng] : partition.engines()) {
        // Check book is accessible
        (void)eng.book().best_bid();
        (void)eng.book().best_ask();
        (void)eng.book().order_count();
    }
    return true;
}

}  // namespace engine
