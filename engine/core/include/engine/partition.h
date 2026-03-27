#pragma once

#include "engine/journal.h"
#include "engine/matching.h"
#include "engine/order_pool.h"
#include "engine/types.h"

#include <unordered_map>
#include <functional>
#include <vector>

namespace engine {

/// Inbound message to the partition — generic envelope.
/// Asset-class-agnostic. The message carries instrument_id which maps
/// to a MatchingEngine instance inside the partition.
struct PartitionMessage {
    enum class Type : uint8_t {
        NewOrder   = 1,
        Cancel     = 2,
        Modify     = 3,
        MassQuote  = 4,
    };

    Type          type;
    SequenceNum   sequence;
    Timestamp     timestamp;

    ClientOrderId client_order_id;
    InstrumentId  instrument_id;
    Side          side;
    OrderType     order_type;
    TimeInForce   time_in_force;
    Price         price;
    Quantity      quantity;
    ParticipantId participant_id;
    AccountId     account_id;
    SessionId     session_id;
    uint32_t      stp_group_id;
    uint16_t      flags;
    Timestamp     expiry_time;    // GTD expiry (0 = not applicable)
    Price         stop_price = 0; // Stop trigger price (0 = not a stop)

    OrderId       orig_order_id;
    Price         new_price;
    Quantity      new_qty;

    // Mass quote fields
    Price         bid_price = 0;
    Quantity      bid_qty = 0;
    Price         ask_price = 0;
    Quantity      ask_qty = 0;
};


/// Callback interface for partition output events.
struct PartitionListener {
    virtual ~PartitionListener() = default;
    virtual void on_result(SequenceNum seq, const PartitionMessage& msg,
                           const MatchResult& result) = 0;
};


/// A partition owns one or more instruments and processes messages
/// sequentially on a single thread.
///
/// Journal integration (this version):
///   - Partition holds a non-owning pointer to an IJournal.
///   - On each process() call:
///       1. Append INPUT record to journal (the inbound command)
///       2. Execute matching logic (deterministic)
///       3. Append OUTPUT record to journal (the match result summary)
///   - In Regulatory-Grade mode (production), the INPUT append
///     blocks until durable (fsync). The stub returns immediately.
///   - The journal is the single source of truth for replay.
class Partition {
public:
    /// Create a partition with the given ID, pool capacity, and journal.
    /// journal may be nullptr for legacy tests; new code should always provide one.
    Partition(PartitionId id, uint32_t pool_capacity, IJournal* journal = nullptr);

    void add_instrument(const InstrumentConfig& config);
    MatchResult process(const PartitionMessage& msg);

    void set_listener(PartitionListener* listener) noexcept { listener_ = listener; }
    void set_journal(IJournal* journal) noexcept { journal_ = journal; }

    // ─── Queries ────────────────────────────────────────────

    [[nodiscard]] PartitionId id() const noexcept { return id_; }
    [[nodiscard]] SequenceNum sequence() const noexcept { return sequence_; }
    [[nodiscard]] uint32_t instrument_count() const noexcept;
    [[nodiscard]] const MatchingEngine* get_engine(InstrumentId id) const noexcept;
    [[nodiscard]] MatchingEngine* get_engine_mut(InstrumentId id) noexcept;
    [[nodiscard]] SequenceNum snapshot_sequence() const noexcept { return sequence_; }
    [[nodiscard]] const OrderPool& pool() const noexcept { return pool_; }

    /// Reset partition for snapshot restore.
    void reset_for_restore(SequenceNum restored_seq);

    /// Direct order insertion for snapshot restore (bypasses matching/validation).
    bool insert_restored_order(InstrumentId inst_id, const Order& order_data);

    /// Iterate all instruments. For snapshot capture.
    [[nodiscard]] const std::unordered_map<InstrumentId, MatchingEngine>& engines() const noexcept {
        return engines_;
    }

private:
    PartitionId   id_;
    SequenceNum   sequence_ = 0;
    OrderPool     pool_;
    IJournal*     journal_ = nullptr;

    std::unordered_map<InstrumentId, MatchingEngine> engines_;
    PartitionListener* listener_ = nullptr;

    MatchResult dispatch_new_order(const PartitionMessage& msg);
    MatchResult dispatch_cancel(const PartitionMessage& msg);
    MatchResult dispatch_modify(const PartitionMessage& msg);
    MatchResult dispatch_mass_quote(const PartitionMessage& msg);

    /// Append a journal entry for the given message/result.
    void journal_input(const PartitionMessage& msg);
    void journal_output(const PartitionMessage& msg, const MatchResult& result);
};

}  // namespace engine
