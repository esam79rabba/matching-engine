#pragma once

#include "engine/types.h"
#include "engine/matching.h"
#include "engine/partition.h"
#include "engine/session_lifecycle.h"

namespace engine {

/// Instrument listing/delisting controller.
///
/// Manages the lifecycle of instruments at runtime:
///   - List: add instrument to partition + ref data (PRE_LISTED → LISTED)
///   - Activate: transition to tradable state (LISTED → PRE_OPEN or CONTINUOUS)
///   - Deactivate: halt or suspend instrument
///   - Delist: cancel all orders + transition to DELISTED + remove from active set
///
/// All order cancellations go through the normal partition path (deterministic, journaled).
///
/// Generic — no asset-class-specific logic.
class InstrumentLifecycle {
public:

    /// List a new instrument on a partition. Starts in PRE_LISTED state.
    /// Returns true if successfully added.
    static bool list_instrument(Partition& partition, const InstrumentConfig& config);

    /// Activate an instrument for trading. Transitions to CONTINUOUS.
    /// Requires instrument to be in a state that can reach CONTINUOUS
    /// (typically PRE_LISTED → Inactive → PreOpen → Continuous, or force).
    /// For prototype: force-sets to Continuous for simplicity.
    static bool activate_instrument(Partition& partition, InstrumentId id);

    /// Delist an instrument. Cancels all resting orders, transitions to CLOSED.
    /// Returns number of orders cancelled.
    static uint32_t delist_instrument(Partition& partition, InstrumentId id,
                                       Timestamp current_time);

    /// Check if an instrument exists on a partition.
    static bool instrument_exists(const Partition& partition, InstrumentId id);
};

}  // namespace engine
