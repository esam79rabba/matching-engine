#include "engine/kill_switch.h"

namespace engine {

void KillSwitchController::register_partition(Partition* partition) {
    if (partition) partitions_.push_back(partition);
}

void KillSwitchController::set_block_callback(BlockFunc block, UnblockFunc unblock) {
    block_fn_ = std::move(block);
    unblock_fn_ = std::move(unblock);
}

void KillSwitchController::set_cod_controller(CancelOnDisconnect* cod) {
    cod_ = cod;
}

uint32_t KillSwitchController::activate(ParticipantId participant_id) {
    active_.insert(participant_id);
    if (block_fn_) block_fn_(participant_id);

    uint32_t cancelled = 0;
    for (auto* partition : partitions_) {
        std::vector<OrderId> to_cancel;
        for (const auto& [inst_id, eng] : partition->engines()) {
            eng.book().for_each_order([&](const Order& order) {
                if (order.participant_id == participant_id) {
                    to_cancel.push_back(order.order_id);
                }
            });
        }
        for (OrderId oid : to_cancel) {
            PartitionMessage cancel_msg{};
            cancel_msg.type = PartitionMessage::Type::Cancel;
            cancel_msg.timestamp = 0;
            cancel_msg.orig_order_id = oid;
            auto result = partition->process(cancel_msg);
            if (result.action == MatchResult::Action::Cancelled) {
                ++cancelled;
                if (cod_) cod_->untrack_order(oid);
            }
        }
    }
    return cancelled;
}

void KillSwitchController::deactivate(ParticipantId participant_id) {
    active_.erase(participant_id);
    if (unblock_fn_) unblock_fn_(participant_id);
}

bool KillSwitchController::is_active(ParticipantId participant_id) const {
    return active_.count(participant_id) > 0;
}

}  // namespace engine
