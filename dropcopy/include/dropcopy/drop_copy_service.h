#pragma once

#include "engine/types.h"
#include <vector>
#include <unordered_map>
#include <functional>

namespace dropcopy {

/// Normalized drop-copy event — participant-facing.
/// Contains only the fields a participant needs to see about their
/// own orders and executions. No internal engine state leaks.
///
/// Generic — no asset-class-specific fields.
struct DropCopyEvent {
    enum class Type : uint8_t {
        OrderNew      = 1,
        OrderFill     = 2,
        OrderCancel   = 3,
        OrderReject   = 4,
        OrderModify   = 5,
    };

    Type                  type;
    engine::OrderId       order_id;
    engine::ClientOrderId client_order_id;
    engine::InstrumentId  instrument_id;
    engine::ParticipantId participant_id;
    engine::Side          side;

    // Fill-specific (populated when type == OrderFill)
    engine::Price         fill_price = 0;
    engine::Quantity      fill_qty = 0;
    engine::Quantity      cum_qty = 0;
    engine::Quantity      leaves_qty = 0;

    // Reject-specific (populated when type == OrderReject)
    engine::RejectCode    reject_code = engine::RejectCode::None;

    engine::Timestamp     timestamp = 0;
    uint64_t              sequence = 0;   // Drop-copy sequence number
};


/// Abstract drop-copy delivery sink — per participant.
/// Production: FIX drop-copy session. Test: in-memory recorder.
class IDropCopySink {
public:
    virtual ~IDropCopySink() = default;
    virtual void deliver(const DropCopyEvent& event) = 0;
};


/// Drop copy service — routes engine output events to participant-specific sinks.
///
/// Architecture:
///   - Receives normalized events from the engine output path
///   - Filters by participant_id → routes to the correct sink
///   - Assigns per-service sequence numbers for gap detection
///   - Does NOT interpret or modify event content
///   - Read-only from participant's perspective (no inbound messages)
///
/// Not on the engine hot path. Latency target: < 100 µs from engine event.
class DropCopyService {
public:
    DropCopyService();

    /// Register a sink for a participant.
    void register_sink(engine::ParticipantId participant_id, IDropCopySink* sink);

    /// Remove a participant's sink (e.g., on session close).
    void unregister_sink(engine::ParticipantId participant_id);

    // ─── Event ingress (called by output-bus consumer) ──────

    void on_order_new(engine::OrderId order_id, engine::ClientOrderId client_order_id,
                      engine::InstrumentId instrument_id, engine::ParticipantId participant_id,
                      engine::Side side, engine::Timestamp timestamp);

    void on_order_fill(engine::OrderId order_id, engine::ClientOrderId client_order_id,
                       engine::InstrumentId instrument_id, engine::ParticipantId participant_id,
                       engine::Side side, engine::Price fill_price, engine::Quantity fill_qty,
                       engine::Quantity cum_qty, engine::Quantity leaves_qty,
                       engine::Timestamp timestamp);

    void on_order_cancel(engine::OrderId order_id, engine::ClientOrderId client_order_id,
                         engine::InstrumentId instrument_id, engine::ParticipantId participant_id,
                         engine::Side side, engine::Timestamp timestamp);

    void on_order_reject(engine::ClientOrderId client_order_id,
                         engine::InstrumentId instrument_id, engine::ParticipantId participant_id,
                         engine::RejectCode code, engine::Timestamp timestamp);

    [[nodiscard]] uint64_t sequence() const noexcept { return sequence_; }

private:
    uint64_t sequence_ = 0;
    std::unordered_map<engine::ParticipantId, IDropCopySink*> sinks_;

    void route(engine::ParticipantId participant_id, DropCopyEvent& event);
};


/// Test sink: records all events for a participant.
class TestDropCopySink : public IDropCopySink {
public:
    std::vector<DropCopyEvent> events;
    void deliver(const DropCopyEvent& event) override { events.push_back(event); }
};

}  // namespace dropcopy
