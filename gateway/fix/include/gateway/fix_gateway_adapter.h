#pragma once

#include "engine/types.h"
#include "engine/partition.h"

#include <string>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace gateway {

/// Internal representation of a parsed new-order command.
/// Completely independent of FIX — no FIX types leak through this struct.
///
/// The FIX adapter translates FIX messages into these internal commands.
/// The same internal command structure would be used by the binary (SBE)
/// gateway, REST/WebSocket gateway, or any future protocol adapter.
struct NewOrderCommand {
    engine::ClientOrderId client_order_id;
    std::string           raw_client_order_id;  // Original string ClOrdID from FIX
    engine::InstrumentId  instrument_id;
    engine::Side          side;
    engine::OrderType     order_type;
    engine::TimeInForce   time_in_force;
    engine::Price         price;           // Integer-encoded (0 for market)
    engine::Quantity      quantity;
    engine::ParticipantId participant_id;
    engine::AccountId     account_id;
    engine::SessionId     session_id;
    uint32_t              stp_group_id;
    uint16_t              flags;
};

/// Internal representation of a parsed cancel command.
struct CancelCommand {
    engine::OrderId       orig_order_id;
    engine::ClientOrderId client_order_id;    // Client's cancel request ID
    std::string           raw_client_order_id;  // Original string ClOrdID
    std::string           raw_orig_order_id;    // Original string OrigClOrdID
    engine::InstrumentId  instrument_id;      // For routing to correct partition
};

/// Internal representation of a parsed cancel/replace (modify) command.
struct ModifyCommand {
    engine::OrderId       orig_order_id;
    engine::ClientOrderId client_order_id;
    engine::InstrumentId  instrument_id;
    engine::Price         new_price;          // 0 = no change
    engine::Quantity      new_quantity;        // 0 = no change
};

/// Order status request (MsgType H)
struct OrderStatusCommand {
    engine::OrderId order_id;
    engine::ClientOrderId client_order_id;
    std::string raw_client_order_id;
    engine::InstrumentId instrument_id;
};

/// Mass cancel request (MsgType q)
struct MassCancelCommand {
    enum class Scope : uint8_t {
        ByInstrument = 1,
        ByParticipant = 7,
        All = 7
    };
    Scope scope;
    engine::InstrumentId instrument_id;   // For ByInstrument
    engine::ParticipantId participant_id;  // For ByParticipant
    engine::ClientOrderId client_order_id;
    std::string raw_client_order_id;
};

/// Reject information returned from the adapter when parsing fails.
struct AdapterReject {
    engine::RejectCode code;
    std::string        text;
};


/// Abstract FIX gateway adapter interface.
///
/// This is the ADAPTER BOUNDARY between wire-protocol-specific code
/// (QuickFIX, custom FIX, SBE binary, etc.) and the engine's internal
/// command model.
///
/// The adapter:
///   1. Receives raw FIX messages (or any protocol messages)
///   2. Translates them into internal commands (NewOrderCommand, etc.)
///   3. Submits them to the engine via the EngineSubmitter callback
///   4. Receives execution results and translates back to FIX responses
///
/// What lives INSIDE the adapter (protocol-specific):
///   - FIX message parsing (field extraction, data dictionary)
///   - FIX session management (logon, logout, heartbeat, sequence)
///   - Symbol → InstrumentId lookup (via reference data)
///   - Price/Quantity decimal → integer conversion (via instrument config)
///   - FIX execution report construction
///
/// What stays OUTSIDE the adapter (generic):
///   - Matching logic
///   - Risk checks
///   - Journal
///   - Market data
///   - All engine internals
class IFixGatewayAdapter {
public:
    virtual ~IFixGatewayAdapter() = default;

    // ─── Inbound: FIX → Internal ────────────────────────────

    /// Parse a FIX NewOrderSingle (MsgType D) into an internal command.
    /// Returns nullopt if the message is malformed or missing required fields.
    [[nodiscard]]
    virtual std::optional<NewOrderCommand> parse_new_order(
        const std::string& fix_message) const = 0;

    /// Parse a FIX OrderCancelRequest (MsgType F).
    [[nodiscard]]
    virtual std::optional<CancelCommand> parse_cancel(
        const std::string& fix_message) const = 0;

    /// Parse a FIX OrderCancelReplaceRequest (MsgType G).
    [[nodiscard]]
    virtual std::optional<ModifyCommand> parse_modify(
        const std::string& fix_message) const = 0;

    // ─── Outbound: Internal → FIX ───────────────────────────

    /// Build a FIX ExecutionReport (MsgType 8) for an order acknowledgment.
    [[nodiscard]]
    virtual std::string build_new_ack(
        const NewOrderCommand& cmd, engine::OrderId venue_order_id) const = 0;

    /// Build a FIX ExecutionReport for a fill.
    [[nodiscard]]
    virtual std::string build_fill(
        engine::OrderId order_id, engine::Price fill_price,
        engine::Quantity fill_qty, engine::Quantity cum_qty,
        engine::Quantity leaves_qty, engine::Side side) const = 0;

    /// Build a FIX ExecutionReport for a cancel confirmation.
    [[nodiscard]]
    virtual std::string build_cancel_ack(engine::OrderId order_id) const = 0;

    /// Build a FIX ExecutionReport for a rejection.
    [[nodiscard]]
    virtual std::string build_reject(
        engine::ClientOrderId client_order_id,
        engine::RejectCode code) const = 0;

    // ─── Inbound: Extended message types ─────────────────────

    /// Parse a FIX OrderStatusRequest (MsgType H).
    [[nodiscard]]
    virtual std::optional<OrderStatusCommand> parse_order_status_request(
        const std::string& fix_message) const { (void)fix_message; return std::nullopt; }

    /// Parse a FIX OrderMassCancelRequest (MsgType q).
    [[nodiscard]]
    virtual std::optional<MassCancelCommand> parse_mass_cancel_request(
        const std::string& fix_message) const { (void)fix_message; return std::nullopt; }

    // ─── Outbound: Extended message types ─────────────────────

    /// Build a FIX ExecutionReport (MsgType 8) for an order status response (ExecType=I).
    [[nodiscard]]
    virtual std::string build_order_status_response(
        engine::OrderId order_id, engine::Side side, engine::Price price,
        engine::Quantity remaining, engine::Quantity cumulative,
        const std::string& status) const { (void)order_id; (void)side; (void)price; (void)remaining; (void)cumulative; (void)status; return {}; }

    /// Build a FIX OrderMassCancelReport (MsgType r).
    [[nodiscard]]
    virtual std::string build_mass_cancel_report(
        engine::ClientOrderId client_order_id,
        uint32_t cancelled_count) const { (void)client_order_id; (void)cancelled_count; return {}; }

    /// Build a FIX TradeCaptureReport (MsgType AE).
    [[nodiscard]]
    virtual std::string build_trade_capture_report(
        engine::TradeId trade_id, engine::OrderId buy_id, engine::OrderId sell_id,
        engine::Price price, engine::Quantity quantity,
        engine::InstrumentId instrument_id) const { (void)trade_id; (void)buy_id; (void)sell_id; (void)price; (void)quantity; (void)instrument_id; return {}; }

    /// Build a FIX BusinessMessageReject (MsgType j).
    [[nodiscard]]
    virtual std::string build_business_reject(
        const std::string& ref_msg_type,
        const std::string& reason) const { (void)ref_msg_type; (void)reason; return {}; }

    /// Build a FIX MarketDataSnapshotFullRefresh (MsgType W).
    [[nodiscard]]
    virtual std::string build_market_data_snapshot(
        engine::InstrumentId instrument_id,
        const std::vector<std::pair<engine::Price, engine::Quantity>>& bids,
        const std::vector<std::pair<engine::Price, engine::Quantity>>& asks) const { (void)instrument_id; (void)bids; (void)asks; return {}; }

    // ─── Session hooks ──────────────────────────────────────

    /// Called when a FIX session is established (logon accepted).
    virtual void on_session_logon(engine::SessionId session_id,
                                   engine::ParticipantId participant_id) = 0;

    /// Called when a FIX session is terminated (logout or disconnect).
    virtual void on_session_logout(engine::SessionId session_id) = 0;

    /// Called when a FIX session is disconnected unexpectedly.
    /// The gateway layer uses this to trigger cancel-on-disconnect.
    virtual void on_session_disconnect(engine::SessionId session_id) = 0;
};


/// Stub FIX adapter for development and testing.
///
/// This stub does NOT actually parse FIX messages. It provides
/// a skeleton implementation that returns hardcoded/test values.
/// The real implementation will use QuickFIX C++ (working assumption)
/// to parse FIX wire messages.
///
/// Purpose:
///   - Verify the adapter boundary compiles and links
///   - Allow engine-side integration tests without a real FIX stack
///   - Placeholder until QuickFIX is integrated
class FixGatewayAdapterStub : public IFixGatewayAdapter {
public:
    std::optional<NewOrderCommand> parse_new_order(
        const std::string& fix_message) const override;

    std::optional<CancelCommand> parse_cancel(
        const std::string& fix_message) const override;

    std::optional<ModifyCommand> parse_modify(
        const std::string& fix_message) const override;

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

    void on_session_logon(engine::SessionId session_id,
                          engine::ParticipantId participant_id) override;
    void on_session_logout(engine::SessionId session_id) override;
    void on_session_disconnect(engine::SessionId session_id) override;

    // ─── Test inspection ────────────────────────────────────
    uint32_t logon_count = 0;
    uint32_t logout_count = 0;
    uint32_t disconnect_count = 0;
};

}  // namespace gateway
