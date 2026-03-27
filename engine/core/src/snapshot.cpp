#include "engine/snapshot.h"

namespace engine {
namespace snapshot {

SnapshotOrder serialize_order(const Order& order) {
    SnapshotOrder so{};
    so.order_id         = order.order_id;
    so.client_order_id  = order.client_order_id;
    so.participant_id   = order.participant_id;
    so.account_id       = order.account_id;
    so.session_id       = order.session_id;
    so.instrument_id    = order.instrument_id;
    so.side             = order.side;
    so.order_type       = order.order_type;
    so.time_in_force    = order.time_in_force;
    so.price            = order.price;
    so.original_qty     = order.original_qty;
    so.remaining_qty    = order.remaining_qty;
    so.cumulative_filled = order.cumulative_filled;
    so.stp_group_id     = order.stp_group_id;
    so.flags            = order.flags;
    so.entry_sequence   = order.entry_sequence;
    so.entry_timestamp  = order.entry_timestamp;
    so.expiry_time      = order.expiry_time;
    return so;
}

void deserialize_order(Order* dest, const SnapshotOrder& src) {
    dest->clear();
    dest->order_id         = src.order_id;
    dest->client_order_id  = src.client_order_id;
    dest->participant_id   = src.participant_id;
    dest->account_id       = src.account_id;
    dest->session_id       = src.session_id;
    dest->instrument_id    = src.instrument_id;
    dest->side             = src.side;
    dest->order_type       = src.order_type;
    dest->time_in_force    = src.time_in_force;
    dest->price            = src.price;
    dest->original_qty     = src.original_qty;
    dest->remaining_qty    = src.remaining_qty;
    dest->cumulative_filled = src.cumulative_filled;
    dest->stp_group_id     = src.stp_group_id;
    dest->flags            = src.flags;
    dest->entry_sequence   = src.entry_sequence;
    dest->entry_timestamp  = src.entry_timestamp;
    dest->expiry_time      = src.expiry_time;
}

Snapshot capture(const Partition& partition) {
    Snapshot snap;
    snap.partition_id = partition.id();
    snap.sequence     = partition.snapshot_sequence();
    snap.timestamp    = 0;  // Production: inject PTP clock

    // Iterate all instruments using the engines() accessor
    for (const auto& [inst_id, engine] : partition.engines()) {
        SnapshotBook book;
        book.instrument_id = inst_id;

        // Use the for_each_order iterator for deterministic traversal
        engine.book().for_each_order([&](const Order& order) {
            book.orders.push_back(serialize_order(order));
        });

        snap.books.push_back(std::move(book));
    }

    return snap;
}

bool restore(Partition& partition, const Snapshot& snap) {
    // Set partition sequence to snapshot point
    partition.reset_for_restore(snap.sequence);

    // Re-insert all orders from the snapshot
    for (const auto& book : snap.books) {
        for (const auto& so : book.orders) {
            Order order{};
            deserialize_order(&order, so);
            if (!partition.insert_restored_order(book.instrument_id, order)) {
                return false;  // Restore failed (pool exhausted or unknown instrument)
            }
        }
    }

    return true;
}

}  // namespace snapshot
}  // namespace engine
