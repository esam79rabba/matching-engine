#pragma once

#include "engine/types.h"
#include "engine/partition.h"
#include "engine/cancel_on_disconnect.h"

#include <vector>
#include <unordered_set>
#include <functional>

namespace engine {

/// Kill switch controller — end-to-end participant trading block.
///
/// Uses callback functions for blocking/unblocking so it does NOT
/// depend on the risk module directly (avoids circular dependency).
/// The caller wires these to risk_svc->block_participant() etc.
///
/// Generic — no asset-class-specific logic.
class KillSwitchController {
public:
    using BlockFunc = std::function<void(ParticipantId)>;
    using UnblockFunc = std::function<void(ParticipantId)>;

    void register_partition(Partition* partition);
    void set_block_callback(BlockFunc block, UnblockFunc unblock);
    void set_cod_controller(CancelOnDisconnect* cod);

    uint32_t activate(ParticipantId participant_id);
    void deactivate(ParticipantId participant_id);
    [[nodiscard]] bool is_active(ParticipantId participant_id) const;

private:
    std::vector<Partition*> partitions_;
    BlockFunc block_fn_;
    UnblockFunc unblock_fn_;
    CancelOnDisconnect* cod_ = nullptr;
    std::unordered_set<ParticipantId> active_;
};

}  // namespace engine
