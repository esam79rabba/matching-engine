#include "refdata/ref_data_http.h"
#include <sstream>
#include <cstdlib>
#include <algorithm>

namespace refdata {

// ─── Helper: InstrumentState → string ─────────────────────────────

static const char* state_to_string(engine::InstrumentState s) {
    switch (s) {
        case engine::InstrumentState::Inactive:       return "Inactive";
        case engine::InstrumentState::PreOpen:         return "PreOpen";
        case engine::InstrumentState::Auction:         return "Auction";
        case engine::InstrumentState::Continuous:      return "Continuous";
        case engine::InstrumentState::Halted:          return "Halted";
        case engine::InstrumentState::Suspended:       return "Suspended";
        case engine::InstrumentState::ClosingAuction:  return "ClosingAuction";
        case engine::InstrumentState::Closed:          return "Closed";
        case engine::InstrumentState::PostClose:       return "PostClose";
        default:                                        return "Unknown";
    }
}

static const char* algo_to_string(engine::MatchingAlgorithm a) {
    switch (a) {
        case engine::MatchingAlgorithm::PriceTimeFIFO: return "FIFO";
        case engine::MatchingAlgorithm::ProRata:       return "ProRata";
        default:                                        return "Unknown";
    }
}

RefDataHttpHandler::RefDataHttpHandler(RefDataService& svc) : svc_(svc), svc_mut_(svc) {}

HttpResponse RefDataHttpHandler::handle(const HttpRequest& req) {
    // ─── POST routes (write) ─────────────────────────────────
    if (req.method == "POST") {
        if (req.path == "/api/v1/instruments")
            return handle_create_instrument(req.body);
        if (req.path == "/api/v1/participants")
            return handle_create_participant(req.body);
        return HttpResponse::bad_request("Unknown POST route");
    }

    // ─── PUT routes (update) ─────────────────────────────────
    if (req.method == "PUT") {
        if (req.path.rfind("/api/v1/instruments/", 0) == 0 && req.path.size() > 20) {
            auto id_str = req.path.substr(20);
            return handle_update_instrument(id_str, req.body);
        }
        return HttpResponse::bad_request("Unknown PUT route");
    }

    // ─── GET routes (read) ───────────────────────────────────
    if (req.method != "GET") return HttpResponse::bad_request("Unsupported method");

    // Route: /api/v1/instruments
    if (req.path == "/api/v1/instruments") return handle_instruments();

    // Route: /api/v1/instruments/{id}
    if (req.path.rfind("/api/v1/instruments/", 0) == 0 && req.path.size() > 20) {
        auto id_str = req.path.substr(20);
        return handle_instrument_detail(id_str);
    }

    // Route: /api/v1/symbols/{symbol}
    if (req.path.rfind("/api/v1/symbols/", 0) == 0 && req.path.size() > 16) {
        auto symbol = req.path.substr(16);
        return handle_instrument_by_symbol(symbol);
    }

    // Route: /api/v1/participants/{id}/risk-limits
    if (req.path.rfind("/api/v1/participants/", 0) == 0 &&
        req.path.find("/risk-limits") != std::string::npos) {
        auto id_start = std::string("/api/v1/participants/").size();
        auto id_end = req.path.find("/risk-limits");
        auto id_str = req.path.substr(id_start, id_end - id_start);
        return handle_participant_risk_limits(id_str);
    }

    // Route: /api/v1/participants
    if (req.path == "/api/v1/participants") return handle_participants();

    // Route: /api/v1/participants/{id}
    if (req.path.rfind("/api/v1/participants/", 0) == 0 && req.path.size() > 21) {
        auto id_str = req.path.substr(21);
        return handle_participant_detail(id_str);
    }

    // Route: /api/v1/sessions/{id}
    if (req.path.rfind("/api/v1/sessions/", 0) == 0 && req.path.size() > 17) {
        auto id_str = req.path.substr(17);
        return handle_sessions(id_str);
    }

    return HttpResponse::not_found();
}

HttpResponse RefDataHttpHandler::handle_instruments() {
    auto ids = svc_.list_instruments();
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) oss << ",";
        auto cfg = svc_.get_instrument(ids[i]);
        if (!cfg) continue;
        auto info = svc_.get_instrument_info(ids[i]);

        // Live engine state
        const char* state_str = "Inactive";
        engine::Price best_bid = 0, best_ask = 0, last_price = 0;
        engine::Quantity best_bid_qty = 0, best_ask_qty = 0;
        engine::MatchingAlgorithm algo = cfg->matching_algo;
        if (partitions_) {
            auto part_id = svc_.get_partition(ids[i]);
            auto pit = partitions_->find(part_id);
            if (pit != partitions_->end()) {
                if (auto* eng = pit->second->get_engine(ids[i])) {
                    state_str = state_to_string(eng->state());
                    best_bid = eng->book().best_bid();
                    best_ask = eng->book().best_ask();
                    best_bid_qty = eng->book().best_bid_qty();
                    best_ask_qty = eng->book().best_ask_qty();
                    last_price = eng->last_traded_price();
                    algo = eng->book().matching_algo();
                }
            }
        }

        oss << "{\"instrument_id\":" << cfg->instrument_id
            << ",\"symbol\":\"" << (info ? info->symbol : svc_.get_symbol(ids[i])) << "\""
            << ",\"description\":\"" << (info ? info->description : "") << "\""
            << ",\"currency\":\"" << (info ? info->currency : "") << "\""
            << ",\"asset_class\":\"" << (info ? info->asset_class : "") << "\""
            << ",\"state\":\"" << state_str << "\""
            << ",\"tick_size\":" << cfg->tick_size
            << ",\"lot_size\":" << cfg->lot_size
            << ",\"static_ref_price\":" << cfg->static_ref_price
            << ",\"static_band_pct\":" << cfg->static_band_pct
            << ",\"dynamic_band_pct\":" << cfg->dynamic_band_pct
            << ",\"best_bid\":" << best_bid
            << ",\"best_ask\":" << best_ask
            << ",\"best_bid_qty\":" << best_bid_qty
            << ",\"best_ask_qty\":" << best_ask_qty
            << ",\"last_traded_price\":" << last_price
            << ",\"matching_algorithm\":\"" << algo_to_string(algo) << "\""
            << "}";
    }
    oss << "]";
    return HttpResponse::ok(oss.str());
}

HttpResponse RefDataHttpHandler::handle_instrument_detail(const std::string& id_str) {
    engine::InstrumentId id;
    try { id = static_cast<engine::InstrumentId>(std::stoul(id_str)); }
    catch (...) { return HttpResponse::bad_request("Invalid instrument ID"); }

    auto cfg = svc_.get_instrument(id);
    if (!cfg) return HttpResponse::not_found();
    auto info = svc_.get_instrument_info(id);

    // Live engine state
    const char* state_str = "Inactive";
    engine::Price best_bid = 0, best_ask = 0, last_price = 0;
    engine::Quantity best_bid_qty = 0, best_ask_qty = 0;
    engine::MatchingAlgorithm algo = cfg->matching_algo;
    if (partitions_) {
        auto part_id = svc_.get_partition(id);
        auto pit = partitions_->find(part_id);
        if (pit != partitions_->end()) {
            if (auto* eng = pit->second->get_engine(id)) {
                state_str = state_to_string(eng->state());
                best_bid = eng->book().best_bid();
                best_ask = eng->book().best_ask();
                best_bid_qty = eng->book().best_bid_qty();
                best_ask_qty = eng->book().best_ask_qty();
                last_price = eng->last_traded_price();
                algo = eng->book().matching_algo();
            }
        }
    }

    std::ostringstream oss;
    oss << "{\"instrument_id\":" << cfg->instrument_id
        << ",\"symbol\":\"" << (info ? info->symbol : svc_.get_symbol(id)) << "\""
        << ",\"description\":\"" << (info ? info->description : "") << "\""
        << ",\"currency\":\"" << (info ? info->currency : "") << "\""
        << ",\"asset_class\":\"" << (info ? info->asset_class : "") << "\""
        << ",\"state\":\"" << state_str << "\""
        << ",\"tick_size\":" << cfg->tick_size
        << ",\"lot_size\":" << cfg->lot_size
        << ",\"min_quantity\":" << cfg->min_quantity
        << ",\"max_quantity\":" << cfg->max_quantity
        << ",\"static_ref_price\":" << cfg->static_ref_price
        << ",\"static_band_pct\":" << cfg->static_band_pct
        << ",\"dynamic_band_pct\":" << cfg->dynamic_band_pct
        << ",\"best_bid\":" << best_bid
        << ",\"best_ask\":" << best_ask
        << ",\"best_bid_qty\":" << best_bid_qty
        << ",\"best_ask_qty\":" << best_ask_qty
        << ",\"last_traded_price\":" << last_price
        << ",\"matching_algorithm\":\"" << algo_to_string(algo) << "\""
        << "}";
    return HttpResponse::ok(oss.str());
}

HttpResponse RefDataHttpHandler::handle_instrument_by_symbol(const std::string& symbol) {
    auto id = svc_.resolve_symbol(symbol);
    if (id == 0) return HttpResponse::not_found();
    return handle_instrument_detail(std::to_string(id));
}

HttpResponse RefDataHttpHandler::handle_participants() {
    auto ids = svc_.list_participants();
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) oss << ",";
        auto p = svc_.get_participant(ids[i]);
        if (p) {
            oss << "{\"participant_id\":" << p->id
                << ",\"name\":\"" << p->name << "\""
                << "}";
        }
    }
    oss << "]";
    return HttpResponse::ok(oss.str());
}

HttpResponse RefDataHttpHandler::handle_participant_detail(const std::string& id_str) {
    engine::ParticipantId id;
    try { id = static_cast<engine::ParticipantId>(std::stoul(id_str)); }
    catch (...) { return HttpResponse::bad_request("Invalid participant ID"); }

    auto p = svc_.get_participant(id);
    if (!p) return HttpResponse::not_found();

    std::ostringstream oss;
    oss << "{\"participant_id\":" << p->id
        << ",\"name\":\"" << p->name << "\""
        << ",\"max_order_qty\":" << p->risk_limits.max_order_qty
        << "}";
    return HttpResponse::ok(oss.str());
}

HttpResponse RefDataHttpHandler::handle_sessions(const std::string& id_str) {
    engine::SessionId id;
    try { id = static_cast<engine::SessionId>(std::stoul(id_str)); }
    catch (...) { return HttpResponse::bad_request("Invalid session ID"); }

    auto s = svc_.get_session(id);
    if (!s) return HttpResponse::not_found();

    std::ostringstream oss;
    oss << "{\"session_id\":" << s->session_id
        << ",\"participant_id\":" << s->participant_id
        << ",\"cod_enabled\":" << (s->cod_enabled ? "true" : "false")
        << ",\"rate_limit\":" << s->rate_limit.max_messages_per_window
        << "}";
    return HttpResponse::ok(oss.str());
}

// ─── B10: Risk limits read ───────────────────────────────────

HttpResponse RefDataHttpHandler::handle_participant_risk_limits(const std::string& id_str) {
    engine::ParticipantId id;
    try { id = static_cast<engine::ParticipantId>(std::stoul(id_str)); }
    catch (...) { return HttpResponse::bad_request("Invalid participant ID"); }

    auto p = svc_.get_participant(id);
    if (!p) return HttpResponse::not_found();

    std::ostringstream oss;
    oss << "{\"participant_id\":" << p->id
        << ",\"max_order_qty\":" << p->risk_limits.max_order_qty
        << ",\"max_notional\":" << p->risk_limits.max_notional
        << ",\"max_price_deviation_pct\":" << p->risk_limits.max_price_deviation_pct
        << ",\"max_message_rate\":" << p->risk_limits.max_message_rate
        << "}";
    return HttpResponse::ok(oss.str());
}

// ─── B7: Create instrument ───────────────────────────────────

HttpResponse RefDataHttpHandler::handle_create_instrument(const std::string& body) {
    auto inst_id = extract_json_int(body, "instrument_id");
    auto tick_size = extract_json_int(body, "tick_size");
    auto lot_size = extract_json_int(body, "lot_size");
    auto min_qty = extract_json_int(body, "min_quantity");
    auto max_qty = extract_json_int(body, "max_quantity");
    auto ref_price = extract_json_int(body, "static_ref_price");
    auto band_pct = extract_json_double(body, "static_band_pct");
    auto symbol = extract_json_string(body, "symbol");

    if (inst_id < 0 || tick_size < 0 || lot_size < 0 || min_qty < 0 ||
        max_qty < 0 || ref_price < 0 || band_pct < 0.0) {
        return HttpResponse::bad_request("Missing required fields");
    }

    engine::InstrumentConfig cfg{};
    cfg.instrument_id = static_cast<engine::InstrumentId>(inst_id);
    cfg.tick_size = static_cast<engine::Price>(tick_size);
    cfg.lot_size = static_cast<engine::Quantity>(lot_size);
    cfg.min_quantity = static_cast<engine::Quantity>(min_qty);
    cfg.max_quantity = static_cast<engine::Quantity>(max_qty);
    cfg.static_ref_price = static_cast<engine::Price>(ref_price);
    cfg.static_band_pct = band_pct;
    cfg.residual_policy = engine::ResidualPolicy::Cancel;
    cfg.stp_mode = engine::STPMode::CancelNewest;

    auto description = extract_json_string(body, "description");
    auto currency = extract_json_string(body, "currency");
    auto asset_class = extract_json_string(body, "asset_class");

    svc_mut_.add_instrument(cfg);
    if (!symbol.empty()) {
        svc_mut_.register_symbol(symbol, cfg.instrument_id);
    }

    // Store enriched info
    RefDataService::InstrumentInfo info{};
    info.instrument_id = cfg.instrument_id;
    info.symbol = symbol;
    info.description = description;
    info.currency = currency;
    info.asset_class = asset_class;
    info.config = cfg;
    svc_mut_.set_instrument_info(cfg.instrument_id, info);

    std::ostringstream oss;
    oss << "{\"instrument_id\":" << cfg.instrument_id
        << ",\"symbol\":\"" << symbol << "\""
        << ",\"description\":\"" << description << "\""
        << ",\"tick_size\":" << cfg.tick_size
        << ",\"lot_size\":" << cfg.lot_size
        << ",\"min_quantity\":" << cfg.min_quantity
        << ",\"max_quantity\":" << cfg.max_quantity
        << ",\"static_ref_price\":" << cfg.static_ref_price
        << ",\"static_band_pct\":" << cfg.static_band_pct
        << ",\"status\":\"created\"}";
    return {201, oss.str()};
}

// ─── B7: Update instrument ───────────────────────────────────

HttpResponse RefDataHttpHandler::handle_update_instrument(const std::string& id_str, const std::string& body) {
    engine::InstrumentId id;
    try { id = static_cast<engine::InstrumentId>(std::stoul(id_str)); }
    catch (...) { return HttpResponse::bad_request("Invalid instrument ID"); }

    auto existing = svc_.get_instrument(id);
    if (!existing) return HttpResponse::not_found();

    // Start from existing config and apply overrides
    auto cfg = *existing;
    auto tick = extract_json_int(body, "tick_size");
    auto lot = extract_json_int(body, "lot_size");
    auto min_q = extract_json_int(body, "min_quantity");
    auto max_q = extract_json_int(body, "max_quantity");
    auto ref_p = extract_json_int(body, "static_ref_price");
    auto band = extract_json_double(body, "static_band_pct");

    if (tick >= 0) cfg.tick_size = static_cast<engine::Price>(tick);
    if (lot >= 0) cfg.lot_size = static_cast<engine::Quantity>(lot);
    if (min_q >= 0) cfg.min_quantity = static_cast<engine::Quantity>(min_q);
    if (max_q >= 0) cfg.max_quantity = static_cast<engine::Quantity>(max_q);
    if (ref_p >= 0) cfg.static_ref_price = static_cast<engine::Price>(ref_p);
    if (band >= 0.0) cfg.static_band_pct = band;

    svc_mut_.update_instrument(id, cfg);

    std::ostringstream oss;
    oss << "{\"instrument_id\":" << cfg.instrument_id
        << ",\"tick_size\":" << cfg.tick_size
        << ",\"lot_size\":" << cfg.lot_size
        << ",\"min_quantity\":" << cfg.min_quantity
        << ",\"max_quantity\":" << cfg.max_quantity
        << ",\"static_ref_price\":" << cfg.static_ref_price
        << ",\"static_band_pct\":" << cfg.static_band_pct
        << ",\"status\":\"updated\"}";
    return HttpResponse::ok(oss.str());
}

// ─── B8: Create participant ──────────────────────────────────

HttpResponse RefDataHttpHandler::handle_create_participant(const std::string& body) {
    auto pid = extract_json_int(body, "participant_id");
    auto name = extract_json_string(body, "name");

    if (pid < 0 || name.empty()) {
        return HttpResponse::bad_request("Missing required fields: participant_id, name");
    }

    RefDataService::ParticipantInfo info{};
    info.id = static_cast<engine::ParticipantId>(pid);
    info.name = name;
    info.sender_comp_id = extract_json_string(body, "sender_comp_id");

    // Optional risk limit fields
    auto max_rate = extract_json_int(body, "max_order_rate");
    auto fat_pct = extract_json_double(body, "fat_finger_price_pct");
    auto fat_notional = extract_json_double(body, "fat_finger_max_notional");

    if (max_rate >= 0) info.risk_limits.max_message_rate = static_cast<uint32_t>(max_rate);
    if (fat_pct >= 0.0) info.risk_limits.max_price_deviation_pct = fat_pct;
    if (fat_notional >= 0.0) info.risk_limits.max_notional = fat_notional;

    svc_mut_.add_participant(info);

    std::ostringstream oss;
    oss << "{\"participant_id\":" << info.id
        << ",\"name\":\"" << info.name << "\""
        << ",\"max_message_rate\":" << info.risk_limits.max_message_rate
        << ",\"max_price_deviation_pct\":" << info.risk_limits.max_price_deviation_pct
        << ",\"max_notional\":" << info.risk_limits.max_notional
        << ",\"status\":\"created\"}";
    return {201, oss.str()};
}

// ─── JSON extraction helpers ─────────────────────────────────

std::string RefDataHttpHandler::extract_json_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    auto start = pos + search.size();
    auto end = json.find('"', start);
    if (end == std::string::npos) return "";
    return json.substr(start, end - start);
}

long long RefDataHttpHandler::extract_json_int(const std::string& json, const std::string& key, long long default_val) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return default_val;
    auto start = pos + search.size();
    // Skip whitespace
    while (start < json.size() && (json[start] == ' ' || json[start] == '\t')) ++start;
    if (start >= json.size()) return default_val;
    // Don't parse strings as ints
    if (json[start] == '"') return default_val;
    try { return std::stoll(json.substr(start)); }
    catch (...) { return default_val; }
}

double RefDataHttpHandler::extract_json_double(const std::string& json, const std::string& key, double default_val) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return default_val;
    auto start = pos + search.size();
    while (start < json.size() && (json[start] == ' ' || json[start] == '\t')) ++start;
    if (start >= json.size()) return default_val;
    if (json[start] == '"') return default_val;
    try { return std::stod(json.substr(start)); }
    catch (...) { return default_val; }
}

}  // namespace refdata
