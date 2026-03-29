#include "gateway/fix_parser.h"
#include <sstream>
#include <charconv>

namespace gateway {

// ═══════════════════════════════════════════════════════════════
// FixParser — tag-value parser
// ═══════════════════════════════════════════════════════════════

std::unordered_map<int, std::string> FixParser::parse_fields(std::string_view raw) {
    std::unordered_map<int, std::string> fields;
    size_t pos = 0;

    while (pos < raw.size()) {
        // Find '='
        auto eq = raw.find('=', pos);
        if (eq == std::string_view::npos) break;

        // Find delimiter (SOH \x01 or pipe '|')
        auto delim = raw.find_first_of("|\x01", eq + 1);
        if (delim == std::string_view::npos) delim = raw.size();

        // Parse tag number
        int tag = 0;
        auto tag_str = raw.substr(pos, eq - pos);
        auto [ptr, ec] = std::from_chars(tag_str.data(), tag_str.data() + tag_str.size(), tag);
        if (ec == std::errc{} && tag > 0) {
            fields[tag] = std::string(raw.substr(eq + 1, delim - eq - 1));
        }

        pos = delim + 1;
    }

    return fields;
}

std::optional<std::string> FixParser::get_msg_type(
        const std::unordered_map<int, std::string>& fields) {
    auto it = fields.find(fix_tags::MsgType);
    if (it == fields.end()) return std::nullopt;
    return it->second;
}

// ═══════════════════════════════════════════════════════════════
// RealFixAdapter
// ═══════════════════════════════════════════════════════════════

RealFixAdapter::RealFixAdapter(SymbolResolver symbol_resolver,
                               PriceConverter price_converter,
                               QtyConverter qty_converter,
                               ParticipantResolver participant_resolver)
    : symbol_resolver_(std::move(symbol_resolver))
    , price_converter_(std::move(price_converter))
    , qty_converter_(std::move(qty_converter))
    , participant_resolver_(std::move(participant_resolver)) {}

// ─── Parse helpers ──────────────────────────────────────────

engine::Side RealFixAdapter::parse_side(const std::string& val) {
    return (val == "1") ? engine::Side::Buy : engine::Side::Sell;
}

engine::OrderType RealFixAdapter::parse_ord_type(const std::string& val) {
    if (val == "1") return engine::OrderType::Market;
    return engine::OrderType::Limit;  // "2" and default
}

engine::TimeInForce RealFixAdapter::parse_tif(const std::string& val) {
    if (val == "0") return engine::TimeInForce::Day;
    if (val == "1") return engine::TimeInForce::GTC;
    if (val == "3") return engine::TimeInForce::IOC;
    if (val == "4") return engine::TimeInForce::FOK;
    if (val == "6") return engine::TimeInForce::GTD;
    return engine::TimeInForce::Day;  // Default
}

// ─── Inbound: FIX → Internal ────────────────────────────────

std::optional<NewOrderCommand> RealFixAdapter::parse_new_order(
        const std::string& fix_message) const {
    auto fields = FixParser::parse_fields(fix_message);

    // Required fields
    auto clord_it = fields.find(fix_tags::ClOrdID);
    auto symbol_it = fields.find(fix_tags::Symbol);
    auto side_it = fields.find(fix_tags::Side);
    auto ordtype_it = fields.find(fix_tags::OrdType);
    auto qty_it = fields.find(fix_tags::OrderQty);

    if (clord_it == fields.end() || symbol_it == fields.end() ||
        side_it == fields.end() || ordtype_it == fields.end() ||
        qty_it == fields.end()) {
        return std::nullopt;  // Missing required fields
    }

    NewOrderCommand cmd{};

    // Parse ClOrdID — support both numeric and alphanumeric
    cmd.raw_client_order_id = clord_it->second;
    try { cmd.client_order_id = std::stoull(clord_it->second); }
    catch (...) {
        // Alphanumeric ClOrdID — hash to numeric for internal use
        std::hash<std::string> hasher;
        cmd.client_order_id = static_cast<engine::ClientOrderId>(hasher(clord_it->second));
    }

    // Resolve symbol → InstrumentId
    cmd.instrument_id = symbol_resolver_(symbol_it->second);
    if (cmd.instrument_id == 0) return std::nullopt;  // Unknown symbol

    cmd.side = parse_side(side_it->second);
    cmd.order_type = parse_ord_type(ordtype_it->second);

    // Quantity
    cmd.quantity = qty_converter_(qty_it->second, cmd.instrument_id);
    if (cmd.quantity == 0) return std::nullopt;

    // Price (required for limit, ignored for market)
    auto price_it = fields.find(fix_tags::Price);
    if (cmd.order_type == engine::OrderType::Limit) {
        if (price_it == fields.end()) return std::nullopt;
        cmd.price = price_converter_(price_it->second, cmd.instrument_id);
    } else {
        cmd.price = 0;
    }

    // Optional: TimeInForce
    auto tif_it = fields.find(fix_tags::TimeInForce);
    cmd.time_in_force = tif_it != fields.end()
        ? parse_tif(tif_it->second)
        : engine::TimeInForce::Day;

    // Optional: Account
    auto acct_it = fields.find(fix_tags::Account);
    if (acct_it != fields.end()) {
        try { cmd.account_id = std::stoul(acct_it->second); }
        catch (...) { cmd.account_id = 0; }
    }

    // Optional: STP group (custom tag)
    auto stp_it = fields.find(fix_tags::STPGroupID);
    if (stp_it != fields.end()) {
        try { cmd.stp_group_id = std::stoul(stp_it->second); }
        catch (...) { cmd.stp_group_id = 0; }
    }

    // Optional: PostOnly flag (custom tag)
    auto po_it = fields.find(fix_tags::PostOnlyFlag);
    if (po_it != fields.end() && po_it->second == "Y") {
        cmd.flags |= engine::FLAG_POST_ONLY;
    }

    // Resolve participant_id from SenderCompID (tag 49)
    auto sender_it = fields.find(fix_tags::SenderCompID);
    if (sender_it != fields.end() && participant_resolver_) {
        cmd.participant_id = participant_resolver_(sender_it->second);
    }
    // session_id is set by the caller (daemon's on_message callback) from the
    // acceptor-level session context, not from the FIX message itself.
    // Default to 0 (unset) — the daemon overrides this with the real session_id.
    cmd.session_id = 0;

    return cmd;
}

std::optional<CancelCommand> RealFixAdapter::parse_cancel(
        const std::string& fix_message) const {
    auto fields = FixParser::parse_fields(fix_message);

    auto origclord_it = fields.find(fix_tags::OrigClOrdID);
    auto clord_it = fields.find(fix_tags::ClOrdID);
    auto symbol_it = fields.find(fix_tags::Symbol);

    if (origclord_it == fields.end() || clord_it == fields.end()) {
        return std::nullopt;
    }

    CancelCommand cmd{};
    cmd.raw_orig_order_id = origclord_it->second;
    cmd.raw_client_order_id = clord_it->second;
    try {
        cmd.orig_order_id = std::stoull(origclord_it->second);
    } catch (...) {
        // Alphanumeric OrigClOrdID — hash for internal use
        std::hash<std::string> hasher;
        cmd.orig_order_id = static_cast<engine::OrderId>(hasher(origclord_it->second));
    }
    try {
        cmd.client_order_id = std::stoull(clord_it->second);
    } catch (...) {
        std::hash<std::string> hasher;
        cmd.client_order_id = static_cast<engine::ClientOrderId>(hasher(clord_it->second));
    }

    if (symbol_it != fields.end()) {
        cmd.instrument_id = symbol_resolver_(symbol_it->second);
    }

    return cmd;
}

std::optional<ModifyCommand> RealFixAdapter::parse_modify(
        const std::string& fix_message) const {
    auto fields = FixParser::parse_fields(fix_message);

    auto origclord_it = fields.find(fix_tags::OrigClOrdID);
    auto clord_it = fields.find(fix_tags::ClOrdID);

    if (origclord_it == fields.end() || clord_it == fields.end()) {
        return std::nullopt;
    }

    ModifyCommand cmd{};
    try {
        cmd.orig_order_id = std::stoull(origclord_it->second);
        cmd.client_order_id = std::stoull(clord_it->second);
    } catch (...) { return std::nullopt; }

    auto symbol_it = fields.find(fix_tags::Symbol);
    if (symbol_it != fields.end()) {
        cmd.instrument_id = symbol_resolver_(symbol_it->second);
    }

    auto price_it = fields.find(fix_tags::Price);
    if (price_it != fields.end()) {
        cmd.new_price = price_converter_(price_it->second, cmd.instrument_id);
    }

    auto qty_it = fields.find(fix_tags::OrderQty);
    if (qty_it != fields.end()) {
        cmd.new_quantity = qty_converter_(qty_it->second, cmd.instrument_id);
    }

    return cmd;
}

std::optional<OrderStatusCommand> RealFixAdapter::parse_order_status_request(
        const std::string& fix_message) const {
    auto fields = FixParser::parse_fields(fix_message);

    OrderStatusCommand cmd{};

    // Extract OrderID (tag 37) or ClOrdID (tag 11)
    auto order_it = fields.find(fix_tags::OrderID);
    auto clord_it = fields.find(fix_tags::ClOrdID);

    if (order_it == fields.end() && clord_it == fields.end()) {
        return std::nullopt;  // Need at least one identifier
    }

    if (order_it != fields.end()) {
        try { cmd.order_id = std::stoull(order_it->second); }
        catch (...) {
            std::hash<std::string> hasher;
            cmd.order_id = static_cast<engine::OrderId>(hasher(order_it->second));
        }
    }

    if (clord_it != fields.end()) {
        cmd.raw_client_order_id = clord_it->second;
        try { cmd.client_order_id = std::stoull(clord_it->second); }
        catch (...) {
            std::hash<std::string> hasher;
            cmd.client_order_id = static_cast<engine::ClientOrderId>(hasher(clord_it->second));
        }
    }

    auto symbol_it = fields.find(fix_tags::Symbol);
    if (symbol_it != fields.end()) {
        cmd.instrument_id = symbol_resolver_(symbol_it->second);
    }

    return cmd;
}

std::optional<MassCancelCommand> RealFixAdapter::parse_mass_cancel_request(
        const std::string& fix_message) const {
    auto fields = FixParser::parse_fields(fix_message);

    auto type_it = fields.find(fix_tags::MassCancelRequestType);
    if (type_it == fields.end()) {
        return std::nullopt;
    }

    MassCancelCommand cmd{};

    // MassCancelRequestType: 1=cancel by security, 7=cancel all
    if (type_it->second == "1") {
        cmd.scope = MassCancelCommand::Scope::ByInstrument;
    } else if (type_it->second == "7") {
        cmd.scope = MassCancelCommand::Scope::All;
    } else {
        return std::nullopt;  // Unsupported scope
    }

    // Symbol for ByInstrument scope
    auto symbol_it = fields.find(fix_tags::Symbol);
    if (symbol_it != fields.end()) {
        cmd.instrument_id = symbol_resolver_(symbol_it->second);
    }

    // ClOrdID
    auto clord_it = fields.find(fix_tags::ClOrdID);
    if (clord_it != fields.end()) {
        cmd.raw_client_order_id = clord_it->second;
        try { cmd.client_order_id = std::stoull(clord_it->second); }
        catch (...) {
            std::hash<std::string> hasher;
            cmd.client_order_id = static_cast<engine::ClientOrderId>(hasher(clord_it->second));
        }
    }

    // SenderCompID for participant resolution
    auto sender_it = fields.find(fix_tags::SenderCompID);
    if (sender_it != fields.end() && participant_resolver_) {
        cmd.participant_id = participant_resolver_(sender_it->second);
    }

    return cmd;
}

// ─── Outbound: Internal → FIX ───────────────────────────────

std::string RealFixAdapter::build_new_ack(
        const NewOrderCommand& cmd, engine::OrderId venue_order_id) const {
    std::ostringstream oss;
    oss << "8=FIX.4.4\x01" << "35=8\x01"
        << "150=0\x01" << "39=0\x01"
        << "11=" << (cmd.raw_client_order_id.empty() ? std::to_string(cmd.client_order_id) : cmd.raw_client_order_id) << "\x01"
        << "37=" << venue_order_id << "\x01"
        << "55=" << cmd.instrument_id << "\x01"
        << "54=" << static_cast<int>(cmd.side) << "\x01"
        << "44=" << cmd.price << "\x01"
        << "38=" << cmd.quantity << "\x01";
    oss << "10=000\x01";
    return oss.str();
}

std::string RealFixAdapter::build_fill(
        engine::OrderId order_id, engine::Price fill_price,
        engine::Quantity fill_qty, engine::Quantity cum_qty,
        engine::Quantity leaves_qty, engine::Side side) const {
    std::ostringstream oss;
    oss << "8=FIX.4.4\x01" << "35=8\x01"
        << "150=F\x01"
        << "39=" << (leaves_qty == 0 ? "2" : "1") << "\x01"
        << "37=" << order_id << "\x01"
        << "31=" << fill_price << "\x01"
        << "32=" << fill_qty << "\x01"
        << "14=" << cum_qty << "\x01"
        << "151=" << leaves_qty << "\x01"
        << "54=" << static_cast<int>(side) << "\x01";
    oss << "10=000\x01";
    return oss.str();
}

std::string RealFixAdapter::build_cancel_ack(engine::OrderId order_id) const {
    std::ostringstream oss;
    oss << "8=FIX.4.4\x01" << "35=8\x01"
        << "150=4\x01" << "39=4\x01"
        << "37=" << order_id << "\x01";
    oss << "10=000\x01";
    return oss.str();
}

std::string RealFixAdapter::build_reject(
        engine::ClientOrderId client_order_id,
        engine::RejectCode code) const {
    std::ostringstream oss;
    oss << "8=FIX.4.4\x01" << "35=8\x01"
        << "150=8\x01" << "39=8\x01"
        << "11=" << client_order_id << "\x01"
        << "103=" << static_cast<int>(code) << "\x01";
    oss << "10=000\x01";
    return oss.str();
}

std::string RealFixAdapter::build_order_status_response(
        engine::OrderId order_id, engine::Side side, engine::Price price,
        engine::Quantity remaining, engine::Quantity cumulative,
        const std::string& status) const {
    std::ostringstream oss;
    oss << "8=FIX.4.4\x01" << "35=8\x01"
        << "150=I\x01"
        << "39=" << status << "\x01"
        << "37=" << order_id << "\x01"
        << "54=" << static_cast<int>(side) << "\x01"
        << "44=" << price << "\x01"
        << "38=" << (remaining + cumulative) << "\x01"
        << "14=" << cumulative << "\x01"
        << "151=" << remaining << "\x01";
    oss << "10=000\x01";
    return oss.str();
}

std::string RealFixAdapter::build_mass_cancel_report(
        engine::ClientOrderId client_order_id,
        uint32_t cancelled_count) const {
    std::ostringstream oss;
    oss << "8=FIX.4.4\x01" << "35=r\x01"
        << "11=" << client_order_id << "\x01"
        << "530=7\x01"
        << "531=1\x01"                       // MassCancelResponse: 1=Cancel Request Accepted
        << "533=" << cancelled_count << "\x01";
    oss << "10=000\x01";
    return oss.str();
}

std::string RealFixAdapter::build_trade_capture_report(
        engine::TradeId trade_id, engine::OrderId buy_id, engine::OrderId sell_id,
        engine::Price price, engine::Quantity quantity,
        engine::InstrumentId instrument_id) const {
    std::ostringstream oss;
    oss << "8=FIX.4.4\x01" << "35=AE\x01"
        << "571=" << trade_id << "\x01"
        << "55=" << instrument_id << "\x01"
        << "32=" << quantity << "\x01"
        << "31=" << price << "\x01"
        << "552=2\x01"                        // NoSides = 2 (buyer + seller)
        << "54=1\x01" << "37=" << buy_id << "\x01"   // Side 1 = Buy
        << "54=2\x01" << "37=" << sell_id << "\x01";  // Side 2 = Sell
    oss << "10=000\x01";
    return oss.str();
}

std::string RealFixAdapter::build_business_reject(
        const std::string& ref_msg_type,
        const std::string& reason) const {
    std::ostringstream oss;
    oss << "8=FIX.4.4\x01" << "35=j\x01"
        << "45=0\x01"                         // RefSeqNum (placeholder)
        << "372=" << ref_msg_type << "\x01"
        << "380=3\x01"                        // BusinessRejectReason: 3=Unsupported Message Type
        << "58=" << reason << "\x01";
    oss << "10=000\x01";
    return oss.str();
}

std::string RealFixAdapter::build_market_data_snapshot(
        engine::InstrumentId instrument_id,
        const std::vector<std::pair<engine::Price, engine::Quantity>>& bids,
        const std::vector<std::pair<engine::Price, engine::Quantity>>& asks) const {
    std::ostringstream oss;
    auto total_entries = bids.size() + asks.size();
    oss << "8=FIX.4.4\x01" << "35=W\x01"
        << "55=" << instrument_id << "\x01"
        << "262=0\x01"                         // MDReqID placeholder
        << "268=" << total_entries << "\x01";

    for (const auto& [px, qty] : bids) {
        oss << "269=0\x01"                     // MDEntryType: 0=Bid
            << "270=" << px << "\x01"
            << "271=" << qty << "\x01";
    }
    for (const auto& [px, qty] : asks) {
        oss << "269=1\x01"                     // MDEntryType: 1=Offer
            << "270=" << px << "\x01"
            << "271=" << qty << "\x01";
    }
    oss << "10=000\x01";
    return oss.str();
}

// ─── Session hooks ──────────────────────────────────────────

void RealFixAdapter::on_session_logon(engine::SessionId, engine::ParticipantId) { ++logon_count; }
void RealFixAdapter::on_session_logout(engine::SessionId) { ++logout_count; }
void RealFixAdapter::on_session_disconnect(engine::SessionId) { ++disconnect_count; }

}  // namespace gateway
