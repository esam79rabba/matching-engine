#pragma once

#include "engine/order.h"
#include "engine/order_book.h"
#include "engine/partition.h"
#include "engine/types.h"

#include <vector>
#include <cstdint>

namespace engine {

/// Serialized representation of a single order (for snapshot).
/// Plain data struct — no pointers, no intrusive list links.
///
/// This is a STARTER internal format, not a final production wire format.
/// Production snapshot format will be versioned and may use a binary
/// encoding (e.g., FlatBuffers or custom) for faster deserialization.
struct SnapshotOrder {
    OrderId       order_id;
    ClientOrderId client_order_id;
    ParticipantId participant_id;
    AccountId     account_id;
    SessionId     session_id;
    InstrumentId  instrument_id;
    Side          side;
    OrderType     order_type;
    TimeInForce   time_in_force;
    Price         price;
    Quantity      original_qty;
    Quantity      remaining_qty;
    Quantity      cumulative_filled;
    uint32_t      stp_group_id;
    uint16_t      flags;
    SequenceNum   entry_sequence;
    Timestamp     entry_timestamp;
    Timestamp     expiry_time;
};

/// Serialized representation of a single instrument's book state.
struct SnapshotBook {
    InstrumentId              instrument_id;
    std::vector<SnapshotOrder> orders;    // All resting orders, in entry_sequence order
};

/// Complete partition snapshot.
struct Snapshot {
    PartitionId               partition_id;
    SequenceNum               sequence;    // Partition sequence at snapshot time
    Timestamp                 timestamp;   // When snapshot was captured
    std::vector<SnapshotBook> books;       // One per instrument in the partition

    /// Simple integrity check — order count across all books.
    [[nodiscard]] uint32_t total_order_count() const noexcept {
        uint32_t count = 0;
        for (const auto& b : books) count += static_cast<uint32_t>(b.orders.size());
        return count;
    }
};


/// Snapshot capture and restore utilities.
///
/// These are free functions operating on Partition state.
/// They are NOT part of the hot path — snapshot capture runs
/// on a periodic timer (e.g., every 60 seconds) and may briefly
/// pause the partition thread (or take a consistent read).
///
/// Production replacement: Snapshot to file (NVMe) with versioned
/// binary format, checksums, and atomic write semantics.
namespace snapshot {

    /// Capture a snapshot of the current partition state.
    /// Iterates all instruments and serializes all resting orders.
    Snapshot capture(const Partition& partition);

    /// Restore a partition from a snapshot.
    /// Clears the partition's current state and re-inserts all orders.
    /// After restore, the partition's sequence counter is set to the
    /// snapshot's sequence, and journal replay continues from there.
    ///
    /// Returns true if restoration succeeded, false if any inconsistency found.
    bool restore(Partition& partition, const Snapshot& snap);

    /// Convert an Order (with intrusive pointers) to a SnapshotOrder (plain data).
    SnapshotOrder serialize_order(const Order& order);

    /// Populate an Order from a SnapshotOrder. The caller must provide
    /// the Order from a pool. Intrusive pointers are NOT set — the
    /// order book insertion will handle those.
    void deserialize_order(Order* dest, const SnapshotOrder& src);

}  // namespace snapshot
}  // namespace engine
