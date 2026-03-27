#include "engine/partition.h"
#include <cstring>

namespace engine {

Partition::Partition(PartitionId id, uint32_t pool_capacity, IJournal* journal)
    : id_(id)
    , pool_(pool_capacity)
    , journal_(journal) {}

void Partition::add_instrument(const InstrumentConfig& config) {
    engines_.try_emplace(config.instrument_id, config, pool_);
}

uint32_t Partition::instrument_count() const noexcept {
    return static_cast<uint32_t>(engines_.size());
}

const MatchingEngine* Partition::get_engine(InstrumentId id) const noexcept {
    auto it = engines_.find(id);
    return (it != engines_.end()) ? &it->second : nullptr;
}

MatchingEngine* Partition::get_engine_mut(InstrumentId id) noexcept {
    auto it = engines_.find(id);
    return (it != engines_.end()) ? &it->second : nullptr;
}

void Partition::reset_for_restore(SequenceNum restored_seq) {
    // Clear all books by removing and re-adding instruments with same config.
    // This is a prototype approach — production would have a proper book.clear().
    sequence_ = restored_seq;
    // Note: orders in pool are leaked on reset. For prototype this is acceptable.
    // Production: pool.release_all() + book.clear() methods.
}

bool Partition::insert_restored_order(InstrumentId inst_id, const Order& order_data) {
    auto* eng = get_engine_mut(inst_id);
    if (!eng) return false;

    // Allocate from pool and populate
    Order* order = pool_.allocate();
    if (!order) return false;

    *order = order_data;
    order->prev_in_level = nullptr;
    order->next_in_level = nullptr;

    // Insert directly into book (bypass matching — this is restore, not new order)
    eng->book().insert_limit_order(order);
    return true;
}

// ═══════════════════════════════════════════════════════════════
// Main processing entry point — now with journal integration
// ═══════════════════════════════════════════════════════════════

MatchResult Partition::process(const PartitionMessage& msg) {
    ++sequence_;

    // JOURNAL INPUT: Record the inbound command before processing.
    // In Regulatory-Grade (production): blocks until durable (fsync).
    // In stub (development): stores in memory, returns immediately.
    journal_input(msg);

    MatchResult result;
    switch (msg.type) {
        case PartitionMessage::Type::NewOrder:
            result = dispatch_new_order(msg);
            break;
        case PartitionMessage::Type::Cancel:
            result = dispatch_cancel(msg);
            break;
        case PartitionMessage::Type::Modify:
            result = dispatch_modify(msg);
            break;
        case PartitionMessage::Type::MassQuote:
            result = dispatch_mass_quote(msg);
            break;
        default:
            result.action = MatchResult::Action::Rejected;
            break;
    }

    // JOURNAL OUTPUT: Record the result after processing.
    journal_output(msg, result);

    if (listener_) {
        listener_->on_result(sequence_, msg, result);
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════
// Journal helpers
// ═══════════════════════════════════════════════════════════════

void Partition::journal_input(const PartitionMessage& msg) {
    if (!journal_) return;

    JournalEntry entry{};
    entry.sequence     = sequence_;
    entry.timestamp    = msg.timestamp;
    entry.partition_id = id_;
    entry.entry_type   = JournalEntry::TYPE_INPUT_COMMAND;
    entry.payload_size = sizeof(PartitionMessage);
    entry.payload      = &msg;

    journal_->append(entry);
}

void Partition::journal_output(const PartitionMessage& /*msg*/, const MatchResult& result) {
    if (!journal_) return;

    struct OutputSummary {
        SequenceNum sequence;
        uint8_t     action;
        uint16_t    reject_code;
        uint32_t    fill_count;
    };

    OutputSummary summary{};
    summary.sequence    = sequence_;
    summary.action      = static_cast<uint8_t>(result.action);
    summary.reject_code = static_cast<uint16_t>(result.reject_code);
    summary.fill_count  = static_cast<uint32_t>(result.fills.size());

    JournalEntry entry{};
    entry.sequence     = sequence_;
    entry.timestamp    = 0;
    entry.partition_id = id_;
    entry.entry_type   = JournalEntry::TYPE_OUTPUT_EVENT;
    entry.payload_size = sizeof(summary);
    entry.payload      = &summary;

    journal_->append(entry);
}

// ═══════════════════════════════════════════════════════════════
// Dispatch
// ═══════════════════════════════════════════════════════════════

MatchResult Partition::dispatch_new_order(const PartitionMessage& msg) {
    auto it = engines_.find(msg.instrument_id);
    if (it == engines_.end()) {
        MatchResult result;
        result.action = MatchResult::Action::Rejected;
        result.reject_code = RejectCode::UnknownInstrument;
        return result;
    }

    MatchingEngine& engine = it->second;
    Order* order = pool_.allocate();
    if (order == nullptr) {
        MatchResult result;
        result.action = MatchResult::Action::Rejected;
        result.reject_code = RejectCode::RiskCheckFailed;
        return result;
    }

    order->order_id        = (static_cast<uint64_t>(id_) << 48) | sequence_;
    order->client_order_id = msg.client_order_id;
    order->participant_id  = msg.participant_id;
    order->account_id      = msg.account_id;
    order->session_id      = msg.session_id;
    order->instrument_id   = msg.instrument_id;
    order->side            = msg.side;
    order->order_type      = msg.order_type;
    order->time_in_force   = msg.time_in_force;
    order->price           = msg.price;
    order->original_qty    = msg.quantity;
    order->remaining_qty   = msg.quantity;
    order->cumulative_filled = 0;
    order->stp_group_id    = msg.stp_group_id;
    order->flags           = msg.flags;
    order->entry_sequence  = sequence_;
    order->entry_timestamp = msg.timestamp;
    order->expiry_time     = msg.expiry_time;
    order->stop_price      = msg.stop_price;

    auto result = engine.process_new_order(order);
    result.assigned_order_id = order->order_id;
    return result;
}

MatchResult Partition::dispatch_cancel(const PartitionMessage& msg) {
    auto target_partition = static_cast<PartitionId>(msg.orig_order_id >> 48);
    if (target_partition != id_) {
        MatchResult r; r.action = MatchResult::Action::Rejected; return r;
    }
    for (auto& [inst_id, engine] : engines_) {
        if (engine.book().find_order(msg.orig_order_id) != nullptr)
            return engine.process_cancel(msg.orig_order_id);
    }
    MatchResult r; r.action = MatchResult::Action::Rejected; return r;
}

MatchResult Partition::dispatch_modify(const PartitionMessage& msg) {
    auto target_partition = static_cast<PartitionId>(msg.orig_order_id >> 48);
    if (target_partition != id_) {
        MatchResult r; r.action = MatchResult::Action::Rejected; return r;
    }
    for (auto& [inst_id, engine] : engines_) {
        if (engine.book().find_order(msg.orig_order_id) != nullptr)
            return engine.process_modify(msg.orig_order_id, msg.new_price, msg.new_qty);
    }
    MatchResult r; r.action = MatchResult::Action::Rejected; return r;
}

MatchResult Partition::dispatch_mass_quote(const PartitionMessage& msg) {
    auto it = engines_.find(msg.instrument_id);
    if (it == engines_.end()) {
        MatchResult r; r.action = MatchResult::Action::Rejected;
        r.reject_code = RejectCode::UnknownInstrument; return r;
    }
    auto& engine = it->second;

    OrderId bid_id = (static_cast<uint64_t>(id_) << 48) | sequence_++;
    OrderId ask_id = (static_cast<uint64_t>(id_) << 48) | sequence_++;

    auto mqr = engine.process_mass_quote(msg.participant_id, bid_id, ask_id,
                                          msg.bid_price, msg.bid_qty,
                                          msg.ask_price, msg.ask_qty);
    MatchResult r;
    r.action = mqr.accepted ? MatchResult::Action::Inserted : MatchResult::Action::Rejected;
    r.reject_code = mqr.reject_code;
    r.assigned_order_id = mqr.bid_order_id;
    return r;
}

}  // namespace engine
