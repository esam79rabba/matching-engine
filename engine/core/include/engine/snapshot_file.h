#pragma once

#include "engine/types.h"
#include "engine/partition.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

/// Binary snapshot file format for persisting resting orders across restarts.
///
/// File format (little-endian, field-by-field serialization):
///   [Header]
///     magic: "MESNAP01" (8 bytes)
///     version: uint32_t (1)
///     partition_count: uint32_t
///     total_orders: uint64_t
///     timestamp: uint64_t (nanoseconds)
///   [For each partition]
///     partition_id: uint16_t
///     order_count: uint32_t
///     sequence: uint64_t
///     [For each order]
///       Fixed-size record with all order fields written individually.
///
/// This format is self-describing (magic + version) and does NOT depend
/// on C++ struct layout, ensuring stability across code changes.

class SnapshotFileWriter {
public:
    /// Save all resting orders from all partitions to a file.
    /// Writes to a .tmp file first, then atomically renames.
    /// Returns true on success.
    static bool save(const std::string& path,
                     const std::unordered_map<PartitionId, std::unique_ptr<Partition>>& partitions);
};

class SnapshotFileReader {
public:
    struct OrderData {
        InstrumentId  instrument_id;
        OrderId       order_id;
        ClientOrderId client_order_id;
        ParticipantId participant_id;
        AccountId     account_id;
        SessionId     session_id;
        Side          side;
        OrderType     order_type;
        TimeInForce   time_in_force;
        Price         price;
        Quantity      original_qty;
        Quantity      remaining_qty;
        Quantity      display_qty;
        Quantity      cumulative_filled;
        Price         stop_price;
        Price         trail_offset;
        PegType       peg_type;
        Price         peg_offset;
        Price         peg_limit;
        int64_t       budget;
        int64_t       budget_spent;
        uint32_t      stp_group_id;
        uint16_t      flags;
        SequenceNum   entry_sequence;
        Timestamp     entry_timestamp;
        Timestamp     expiry_time;
    };

    struct PartitionData {
        PartitionId partition_id;
        SequenceNum sequence;
        std::vector<OrderData> orders;
    };

    struct SnapshotData {
        uint32_t version;
        uint64_t timestamp;
        std::vector<PartitionData> partitions;
    };

    /// Load snapshot from file. Returns nullopt if file doesn't exist or is corrupt.
    static std::optional<SnapshotData> load(const std::string& path);
};

}  // namespace engine
