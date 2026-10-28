#include "engine/session_lifecycle.h"

#include <algorithm>

namespace engine {

std::vector<OrderId> SessionLifecycle::collect_day_orders(
        const Partition& partition, InstrumentId instrument_id) {
    std::vector<OrderId> day_orders;
    const auto* eng = partition.get_engine(instrument_id);
    if (!eng) return day_orders;

    eng->book().for_each_order([&](const Order& order) {
        if (order.time_in_force == TimeInForce::Day) {
            day_orders.push_back(order.order_id);
        }
    });

    // Sort for deterministic cancellation order
    std::sort(day_orders.begin(), day_orders.end());
    return day_orders;
}

std::vector<OrderId> SessionLifecycle::collect_expired_gtd_orders(
        const Partition& partition, InstrumentId instrument_id,
        Timestamp current_time) {
    std::vector<OrderId> expired;
    const auto* eng = partition.get_engine(instrument_id);
    if (!eng) return expired;

    eng->book().for_each_order([&](const Order& order) {
        if (order.time_in_force == TimeInForce::GTD &&
            order.expiry_time > 0 && order.expiry_time <= current_time) {
            expired.push_back(order.order_id);
        }
    });

    std::sort(expired.begin(), expired.end());
    return expired;
}

uint32_t SessionLifecycle::on_session_close(
        Partition& partition, InstrumentId instrument_id,
        Timestamp current_time) {
    uint32_t cancelled = 0;

    // Step 1: Cancel all Day orders
    auto day_orders = collect_day_orders(partition, instrument_id);
    for (OrderId oid : day_orders) {
        PartitionMessage cancel{};
        cancel.type = PartitionMessage::Type::Cancel;
        cancel.timestamp = current_time;
        cancel.orig_order_id = oid;
        cancel.instrument_id = instrument_id;
        auto result = partition.process(cancel);
        if (result.action == MatchResult::Action::Cancelled) ++cancelled;
    }

    // Step 2: Cancel expired GTD orders
    auto expired_gtd = collect_expired_gtd_orders(partition, instrument_id, current_time);
    for (OrderId oid : expired_gtd) {
        PartitionMessage cancel{};
        cancel.type = PartitionMessage::Type::Cancel;
        cancel.timestamp = current_time;
        cancel.orig_order_id = oid;
        cancel.instrument_id = instrument_id;
        auto result = partition.process(cancel);
        if (result.action == MatchResult::Action::Cancelled) ++cancelled;
    }

    return cancelled;
}

}  // namespace engine
