#include "engine/instrument_lifecycle.h"

#include <algorithm>

namespace engine {

bool InstrumentLifecycle::list_instrument(Partition& partition,
                                           const InstrumentConfig& config) {
    // Check if already exists
    if (partition.get_engine(config.instrument_id) != nullptr) {
        return false;  // Already listed
    }

    partition.add_instrument(config);

    // New instrument starts in Continuous by default (MatchingEngine constructor).
    // For a proper listing workflow:
    //   Force to Inactive → operator/scheduler transitions to PreOpen → Continuous.
    auto* eng = partition.get_engine_mut(config.instrument_id);
    if (!eng) return false;
    eng->force_state(InstrumentState::Inactive);

    return true;
}

bool InstrumentLifecycle::activate_instrument(Partition& partition, InstrumentId id) {
    auto* eng = partition.get_engine_mut(id);
    if (!eng) return false;

    // Force through the required state path for prototype simplicity
    if (eng->state() == InstrumentState::Inactive) {
        eng->force_state(InstrumentState::PreOpen);
    }
    if (eng->state() == InstrumentState::PreOpen) {
        return eng->transition_state(InstrumentState::Continuous);
    }
    // Already in Continuous or can transition from current state
    if (eng->state() != InstrumentState::Continuous) {
        return eng->transition_state(InstrumentState::Continuous);
    }
    return true;  // Already Continuous
}

uint32_t InstrumentLifecycle::delist_instrument(Partition& partition, InstrumentId id,
                                                  Timestamp current_time) {
    auto* eng = partition.get_engine_mut(id);
    if (!eng) return 0;

    // Cancel all resting orders (Day, GTC, GTD — all cancelled on delist)
    uint32_t cancelled = 0;
    std::vector<OrderId> to_cancel;
    eng->book().for_each_order([&](const Order& order) {
        to_cancel.push_back(order.order_id);
    });
    std::sort(to_cancel.begin(), to_cancel.end());

    for (OrderId oid : to_cancel) {
        PartitionMessage cancel{};
        cancel.type = PartitionMessage::Type::Cancel;
        cancel.timestamp = current_time;
        cancel.orig_order_id = oid;
        cancel.instrument_id = id;
        auto result = partition.process(cancel);
        if (result.action == MatchResult::Action::Cancelled) ++cancelled;
    }

    // Transition to Closed
    // From any state, force to Closed for delisting
    eng->force_state(InstrumentState::Closed);

    return cancelled;
}

bool InstrumentLifecycle::instrument_exists(const Partition& partition, InstrumentId id) {
    return partition.get_engine(id) != nullptr;
}

}  // namespace engine
