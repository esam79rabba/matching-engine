#pragma once

#include "engine/matching.h"
#include "engine/partition.h"
#include "engine/types.h"

namespace engine {

/// Event flowing through the pipeline stages.
/// Carries the original message plus intermediate results from each stage.
struct PipelineEvent {
    /// The original inbound message.
    PartitionMessage message{};

    /// Risk stage output.
    bool risk_passed = false;
    RejectCode risk_reject = RejectCode::None;

    /// Matching stage output.
    MatchResult match_result{};

    /// Routing / bookkeeping.
    InstrumentId instrument_id = 0;
    uint64_t sequence = 0;
};

}  // namespace engine
