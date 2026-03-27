#pragma once

#include "engine/types.h"

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace ops {

/// Entry types for the trade audit trail
enum class AuditEntryType : uint8_t {
    NewOrder   = 1,
    Cancel     = 2,
    Fill       = 3,
    Reject     = 4,
    Modify     = 5,
    MassCancel = 6,
};

/// A single audit trail entry with hash chain integrity
struct AuditEntry {
    uint64_t              sequence;         // Monotonic sequence number
    AuditEntryType        type;
    engine::Timestamp     timestamp;        // Nanosecond timestamp
    engine::ParticipantId participant_id;
    engine::InstrumentId  instrument_id;
    engine::OrderId       order_id;
    engine::Side          side;
    engine::Price         price;
    engine::Quantity      quantity;
    engine::RejectCode    reject_code;      // For rejects
    engine::TradeId       trade_id;         // For fills
    engine::OrderId       contra_order_id;  // For fills (other side)
    std::string           prev_hash;        // SHA-256 hash of previous entry
    std::string           entry_hash;       // SHA-256 hash of this entry
};

/// Complete trade audit trail with SHA-256 hash chain.
/// Every order, cancel, fill, reject, and modify is recorded.
/// Immutable -- entries can never be deleted or modified.
/// Thread-safe via mutex (ops-grade, not hot-path).
class TradeAuditTrail {
public:
    explicit TradeAuditTrail(const std::string& file_path);
    ~TradeAuditTrail();

    /// Record events
    void on_new_order(engine::ParticipantId, engine::InstrumentId, engine::OrderId,
                      engine::Side, engine::Price, engine::Quantity, engine::Timestamp);
    void on_cancel(engine::ParticipantId, engine::InstrumentId, engine::OrderId,
                   engine::Timestamp);
    void on_fill(engine::ParticipantId buyer, engine::ParticipantId seller,
                 engine::InstrumentId, engine::OrderId buy_order, engine::OrderId sell_order,
                 engine::TradeId, engine::Price, engine::Quantity, engine::Timestamp);
    void on_reject(engine::ParticipantId, engine::InstrumentId, engine::OrderId,
                   engine::RejectCode, engine::Timestamp);
    void on_modify(engine::ParticipantId, engine::InstrumentId, engine::OrderId,
                   engine::Price new_price, engine::Quantity new_qty, engine::Timestamp);

    /// Query
    std::vector<AuditEntry> read_recent(size_t count) const;
    std::vector<AuditEntry> read_by_participant(engine::ParticipantId, size_t limit) const;
    std::vector<AuditEntry> read_by_instrument(engine::InstrumentId, size_t limit) const;
    uint64_t entry_count() const;

    /// Integrity verification -- verify entire hash chain
    bool verify_chain() const;

private:
    std::string file_path_;
    mutable std::mutex mutex_;
    std::vector<AuditEntry> entries_;  // In-memory (also persisted to file)
    uint64_t next_sequence_ = 1;
    std::string last_hash_ = "0";     // Genesis hash
    std::ofstream file_;

    std::string compute_hash(const AuditEntry& entry) const;
    void append_entry(AuditEntry entry);
    std::string serialize_entry(const AuditEntry& entry) const;
};

}  // namespace ops
