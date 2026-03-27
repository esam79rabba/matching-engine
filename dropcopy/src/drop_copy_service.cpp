#include "dropcopy/drop_copy_service.h"

namespace dropcopy {

DropCopyService::DropCopyService() = default;

void DropCopyService::register_sink(engine::ParticipantId participant_id, IDropCopySink* sink) {
    if (sink) sinks_[participant_id] = sink;
}

void DropCopyService::unregister_sink(engine::ParticipantId participant_id) {
    sinks_.erase(participant_id);
}

void DropCopyService::route(engine::ParticipantId participant_id, DropCopyEvent& event) {
    ++sequence_;
    event.sequence = sequence_;

    auto it = sinks_.find(participant_id);
    if (it != sinks_.end()) {
        it->second->deliver(event);
    }
    // If no sink registered, event is silently dropped (participant not connected).
}

void DropCopyService::on_order_new(
        engine::OrderId order_id, engine::ClientOrderId client_order_id,
        engine::InstrumentId instrument_id, engine::ParticipantId participant_id,
        engine::Side side, engine::Timestamp timestamp) {
    DropCopyEvent ev{};
    ev.type = DropCopyEvent::Type::OrderNew;
    ev.order_id = order_id;
    ev.client_order_id = client_order_id;
    ev.instrument_id = instrument_id;
    ev.participant_id = participant_id;
    ev.side = side;
    ev.timestamp = timestamp;
    route(participant_id, ev);
}

void DropCopyService::on_order_fill(
        engine::OrderId order_id, engine::ClientOrderId client_order_id,
        engine::InstrumentId instrument_id, engine::ParticipantId participant_id,
        engine::Side side, engine::Price fill_price, engine::Quantity fill_qty,
        engine::Quantity cum_qty, engine::Quantity leaves_qty,
        engine::Timestamp timestamp) {
    DropCopyEvent ev{};
    ev.type = DropCopyEvent::Type::OrderFill;
    ev.order_id = order_id;
    ev.client_order_id = client_order_id;
    ev.instrument_id = instrument_id;
    ev.participant_id = participant_id;
    ev.side = side;
    ev.fill_price = fill_price;
    ev.fill_qty = fill_qty;
    ev.cum_qty = cum_qty;
    ev.leaves_qty = leaves_qty;
    ev.timestamp = timestamp;
    route(participant_id, ev);
}

void DropCopyService::on_order_cancel(
        engine::OrderId order_id, engine::ClientOrderId client_order_id,
        engine::InstrumentId instrument_id, engine::ParticipantId participant_id,
        engine::Side side, engine::Timestamp timestamp) {
    DropCopyEvent ev{};
    ev.type = DropCopyEvent::Type::OrderCancel;
    ev.order_id = order_id;
    ev.client_order_id = client_order_id;
    ev.instrument_id = instrument_id;
    ev.participant_id = participant_id;
    ev.side = side;
    ev.timestamp = timestamp;
    route(participant_id, ev);
}

void DropCopyService::on_order_reject(
        engine::ClientOrderId client_order_id,
        engine::InstrumentId instrument_id, engine::ParticipantId participant_id,
        engine::RejectCode code, engine::Timestamp timestamp) {
    DropCopyEvent ev{};
    ev.type = DropCopyEvent::Type::OrderReject;
    ev.client_order_id = client_order_id;
    ev.instrument_id = instrument_id;
    ev.participant_id = participant_id;
    ev.reject_code = code;
    ev.timestamp = timestamp;
    route(participant_id, ev);
}

}  // namespace dropcopy
