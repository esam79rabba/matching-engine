#pragma once

#include "engine/types.h"
#include "gateway/fix_gateway_adapter.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <optional>
#include <functional>
#include <utility>
#include <vector>

namespace gateway {

/// Lightweight FIX 4.4 tag-value parser.
///
/// Parses real FIX messages in the standard "tag=value|" format
/// (using SOH \x01 or pipe '|' as delimiter).
///
/// This is the FR-1 integration path. It does NOT depend on QuickFIX.
/// When QuickFIX session management is integrated (M3+), it will feed
/// parsed messages into this same adapter via its Application callbacks.
/// The translation logic (FIX tags → internal commands) remains identical.
///
/// Isolation boundary: FIX-specific parsing lives entirely in this file
/// and fix_gateway_adapter. No FIX types or tag numbers leak into
/// the engine core.
class FixParser {
public:
    /// Parse a raw FIX tag-value string into a field map.
    /// Supports both SOH (\x01) and pipe ('|') as delimiters.
    static std::unordered_map<int, std::string> parse_fields(std::string_view raw);

    /// Extract MsgType (tag 35) from parsed fields.
    static std::optional<std::string> get_msg_type(
        const std::unordered_map<int, std::string>& fields);
};


/// Real FIX adapter that parses actual FIX 4.4 messages.
///
/// Replaces the stub adapter for M2 integration testing.
/// Translates real FIX tag-value messages into internal commands.
///
/// Symbol-to-InstrumentId resolution uses a configurable lookup function.
/// In production this queries the reference data service.
class RealFixAdapter : public IFixGatewayAdapter {
public:
    /// Symbol resolver: maps FIX Symbol (tag 55) → InstrumentId.
    /// Returns 0 if symbol not found.
    using SymbolResolver = std::function<engine::InstrumentId(const std::string&)>;

    /// Price converter: maps FIX price string → integer-encoded Price.
    /// Uses instrument's price_multiplier from reference data.
    using PriceConverter = std::function<engine::Price(const std::string&, engine::InstrumentId)>;

    /// Quantity converter: maps FIX quantity string → integer-encoded Quantity.
    using QtyConverter = std::function<engine::Quantity(const std::string&, engine::InstrumentId)>;

    /// Participant resolver: maps SenderCompID (tag 49) → ParticipantId.
    /// Returns 0 if not found.
    using ParticipantResolver = std::function<engine::ParticipantId(const std::string&)>;

    RealFixAdapter(SymbolResolver symbol_resolver,
                   PriceConverter price_converter,
                   QtyConverter qty_converter,
                   ParticipantResolver participant_resolver = {});

    // ─── Inbound: FIX → Internal ────────────────────────────

    std::optional<NewOrderCommand> parse_new_order(
        const std::string& fix_message) const override;

    std::optional<CancelCommand> parse_cancel(
        const std::string& fix_message) const override;

    std::optional<ModifyCommand> parse_modify(
        const std::string& fix_message) const override;

    std::optional<OrderStatusCommand> parse_order_status_request(
        const std::string& fix_message) const override;

    std::optional<MassCancelCommand> parse_mass_cancel_request(
        const std::string& fix_message) const override;

    // ─── Outbound: Internal → FIX ───────────────────────────

    std::string build_new_ack(
        const NewOrderCommand& cmd, engine::OrderId venue_order_id) const override;

    std::string build_fill(
        engine::OrderId order_id, engine::Price fill_price,
        engine::Quantity fill_qty, engine::Quantity cum_qty,
        engine::Quantity leaves_qty, engine::Side side) const override;

    std::string build_cancel_ack(engine::OrderId order_id) const override;

    std::string build_reject(
        engine::ClientOrderId client_order_id,
        engine::RejectCode code) const override;

    std::string build_order_status_response(
        engine::OrderId order_id, engine::Side side, engine::Price price,
        engine::Quantity remaining, engine::Quantity cumulative,
        const std::string& status) const override;

    std::string build_mass_cancel_report(
        engine::ClientOrderId client_order_id,
        uint32_t cancelled_count) const override;

    std::string build_trade_capture_report(
        engine::TradeId trade_id, engine::OrderId buy_id, engine::OrderId sell_id,
        engine::Price price, engine::Quantity quantity,
        engine::InstrumentId instrument_id) const override;

    std::string build_business_reject(
        const std::string& ref_msg_type,
        const std::string& reason) const override;

    std::string build_market_data_snapshot(
        engine::InstrumentId instrument_id,
        const std::vector<std::pair<engine::Price, engine::Quantity>>& bids,
        const std::vector<std::pair<engine::Price, engine::Quantity>>& asks) const override;

    // ─── Session hooks ──────────────────────────────────────

    void on_session_logon(engine::SessionId session_id,
                          engine::ParticipantId participant_id) override;
    void on_session_logout(engine::SessionId session_id) override;
    void on_session_disconnect(engine::SessionId session_id) override;

    // Test inspection
    uint32_t logon_count = 0;
    uint32_t logout_count = 0;
    uint32_t disconnect_count = 0;

private:
    SymbolResolver symbol_resolver_;
    PriceConverter price_converter_;
    QtyConverter   qty_converter_;
    ParticipantResolver participant_resolver_;

    static engine::Side parse_side(const std::string& val);
    static engine::OrderType parse_ord_type(const std::string& val);
    static engine::TimeInForce parse_tif(const std::string& val);
};

// ─── FIX 4.4 Tag Constants ─────────────────────────────────
// Defined here so they don't pollute the engine namespace.
namespace fix_tags {
    constexpr int BeginString    = 8;
    constexpr int MsgType        = 35;
    constexpr int SenderCompID   = 49;
    constexpr int ClOrdID        = 11;
    constexpr int OrigClOrdID    = 41;
    constexpr int OrderID        = 37;
    constexpr int ExecID         = 17;
    constexpr int ExecType       = 150;
    constexpr int OrdStatus      = 39;
    constexpr int Symbol         = 55;
    constexpr int Side           = 54;
    constexpr int OrdType        = 40;
    constexpr int Price          = 44;
    constexpr int OrderQty       = 38;
    constexpr int TimeInForce    = 59;
    constexpr int Account        = 1;
    constexpr int LastPx         = 31;
    constexpr int LastQty        = 32;
    constexpr int CumQty         = 14;
    constexpr int LeavesQty      = 151;
    constexpr int CxlRejReason   = 103;
    constexpr int TransactTime   = 60;
    // Extended message type tags
    constexpr int MassCancelRequestType = 530;
    constexpr int MassCancelResponse    = 531;
    constexpr int TotalAffectedOrders   = 533;
    constexpr int TradeReportID         = 571;
    constexpr int NoSides               = 552;
    constexpr int MDReqID               = 262;
    constexpr int NoMDEntries           = 268;
    constexpr int MDEntryType           = 269;
    constexpr int MDEntryPx             = 270;
    constexpr int MDEntrySize           = 271;
    constexpr int RefMsgType            = 372;
    constexpr int BusinessRejectReason  = 380;
    constexpr int RefSeqNum             = 45;
    constexpr int MsgSeqNum             = 34;
    constexpr int BodyLength            = 9;
    constexpr int CheckSum              = 10;
    constexpr int HeartBtInt            = 108;
    constexpr int TargetCompID          = 56;
    constexpr int SubscriptionRequestType = 263;
    constexpr int MarketDepth           = 264;
    constexpr int Text                  = 58;
    // Custom venue tags
    constexpr int STPGroupID     = 20001;
    constexpr int PostOnlyFlag   = 20004;
}

}  // namespace gateway
