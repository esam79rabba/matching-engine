#include "gateway/fix_gateway_adapter.h"
#include <sstream>

namespace gateway {

// ═══════════════════════════════════════════════════════════════
// FixGatewayAdapterStub — development/testing implementation
// ═══════════════════════════════════════════════════════════════
//
// This stub returns hardcoded test data. In production, the real
// QuickFIX-based adapter will parse actual FIX wire messages and
// build real FIX execution reports.
//
// The stub demonstrates the adapter boundary contract without
// depending on any FIX library.

std::optional<NewOrderCommand> FixGatewayAdapterStub::parse_new_order(
        const std::string& fix_message) const {
    // Stub: return a test order if the message is non-empty
    if (fix_message.empty()) return std::nullopt;

    NewOrderCommand cmd{};
    cmd.client_order_id = 1001;
    cmd.instrument_id   = 100;
    cmd.side            = engine::Side::Buy;
    cmd.order_type      = engine::OrderType::Limit;
    cmd.time_in_force   = engine::TimeInForce::Day;
    cmd.price           = 10000;
    cmd.quantity         = 100;
    cmd.participant_id  = 1;
    cmd.account_id      = 1;
    cmd.session_id      = 1;
    cmd.stp_group_id    = 0;
    cmd.flags           = 0;
    return cmd;
}

std::optional<CancelCommand> FixGatewayAdapterStub::parse_cancel(
        const std::string& fix_message) const {
    if (fix_message.empty()) return std::nullopt;

    CancelCommand cmd{};
    cmd.orig_order_id   = 1;
    cmd.client_order_id = 2001;
    cmd.instrument_id   = 100;
    return cmd;
}

std::optional<ModifyCommand> FixGatewayAdapterStub::parse_modify(
        const std::string& fix_message) const {
    if (fix_message.empty()) return std::nullopt;

    ModifyCommand cmd{};
    cmd.orig_order_id   = 1;
    cmd.client_order_id = 3001;
    cmd.instrument_id   = 100;
    cmd.new_price       = 10050;
    cmd.new_quantity     = 0;  // No quantity change
    return cmd;
}

std::string FixGatewayAdapterStub::build_new_ack(
        const NewOrderCommand& cmd, engine::OrderId venue_order_id) const {
    std::ostringstream oss;
    oss << "8=FIX.4.4|35=8|150=0|39=0"
        << "|11=" << cmd.client_order_id
        << "|37=" << venue_order_id
        << "|55=" << cmd.instrument_id
        << "|54=" << static_cast<int>(cmd.side)
        << "|44=" << cmd.price
        << "|38=" << cmd.quantity
        << "|";
    return oss.str();
}

std::string FixGatewayAdapterStub::build_fill(
        engine::OrderId order_id, engine::Price fill_price,
        engine::Quantity fill_qty, engine::Quantity cum_qty,
        engine::Quantity leaves_qty, engine::Side side) const {
    std::ostringstream oss;
    oss << "8=FIX.4.4|35=8|150=F|39="
        << (leaves_qty == 0 ? "2" : "1")  // 2=Filled, 1=PartialFill
        << "|37=" << order_id
        << "|31=" << fill_price
        << "|32=" << fill_qty
        << "|14=" << cum_qty
        << "|151=" << leaves_qty
        << "|54=" << static_cast<int>(side)
        << "|";
    return oss.str();
}

std::string FixGatewayAdapterStub::build_cancel_ack(engine::OrderId order_id) const {
    std::ostringstream oss;
    oss << "8=FIX.4.4|35=8|150=4|39=4"
        << "|37=" << order_id
        << "|";
    return oss.str();
}

std::string FixGatewayAdapterStub::build_reject(
        engine::ClientOrderId client_order_id,
        engine::RejectCode code) const {
    std::ostringstream oss;
    oss << "8=FIX.4.4|35=8|150=8|39=8"
        << "|11=" << client_order_id
        << "|103=" << static_cast<int>(code)
        << "|";
    return oss.str();
}

void FixGatewayAdapterStub::on_session_logon(
        engine::SessionId /*session_id*/, engine::ParticipantId /*participant_id*/) {
    ++logon_count;
}

void FixGatewayAdapterStub::on_session_logout(engine::SessionId /*session_id*/) {
    ++logout_count;
}

void FixGatewayAdapterStub::on_session_disconnect(engine::SessionId /*session_id*/) {
    ++disconnect_count;
}

}  // namespace gateway
