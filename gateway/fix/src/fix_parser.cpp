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
                               QtyConverter qty_converter)
    : symbol_resolver_(std::move(symbol_resolver))
    , price_converter_(std::move(price_converter))
    , qty_converter_(std::move(qty_converter)) {}

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

    cmd.participant_id = 1;  // TODO: resolve from session
    cmd.session_id = 1;      // TODO: resolve from session

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
    return oss.str();
}

std::string RealFixAdapter::build_cancel_ack(engine::OrderId order_id) const {
    std::ostringstream oss;
    oss << "8=FIX.4.4\x01" << "35=8\x01"
        << "150=4\x01" << "39=4\x01"
        << "37=" << order_id << "\x01";
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
    return oss.str();
}

// ─── Session hooks ──────────────────────────────────────────

void RealFixAdapter::on_session_logon(engine::SessionId, engine::ParticipantId) { ++logon_count; }
void RealFixAdapter::on_session_logout(engine::SessionId) { ++logout_count; }
void RealFixAdapter::on_session_disconnect(engine::SessionId) { ++disconnect_count; }

}  // namespace gateway
