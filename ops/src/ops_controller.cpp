#include "ops/ops_controller.h"
#include "ops/metrics.h"
#include "ops/auth_service.h"
#include "ops/audit_log.h"
#include "ops/session_config_store.h"
#include "ops/trade_audit_trail.h"
#include "posttrade/trade_store.h"
#include "risk/risk_service.h"
#include "surveillance/surveillance_engine.h"
#include "surveillance/alert.h"
#include "eod/eod_processor.h"
#include "eod/eod_file_writer.h"
#include <sstream>
#include <chrono>
#include <iomanip>
#include <ctime>

namespace ops {

void OpsController::register_partition(engine::Partition* partition) {
    if (partition) partitions_.push_back(partition);
}

void OpsController::set_eod_processor(eod::EodProcessor* processor, const std::string& output_dir) {
    eod_processor_ = processor;
    eod_output_dir_ = output_dir;
}

void OpsController::set_auth_service(AuthService* svc) {
    auth_service_ = svc;
}

void OpsController::set_kill_switch(engine::KillSwitchController* ks) {
    kill_switch_ = ks;
}

void OpsController::set_session_query(SessionQueryFn fn) {
    session_query_ = std::move(fn);
}

void OpsController::set_audit_log(AuditLog* log) {
    audit_log_ = log;
}

void OpsController::set_trade_store(posttrade::TradeStore* store) {
    trade_store_ = store;
}

void OpsController::set_session_config_store(SessionConfigStore* store) {
    session_config_store_ = store;
}

void OpsController::set_risk_service(risk::BasicRiskService* svc) {
    risk_service_ = svc;
}

void OpsController::set_surveillance_engine(surveillance::SurveillanceEngine* engine) {
    surveillance_engine_ = engine;
}

void OpsController::set_trade_audit_trail(TradeAuditTrail* trail) {
    trade_audit_trail_ = trail;
}

OpsResponse OpsController::handle(const OpsRequest& req) {
    // Simple path-based routing. Production would use a proper router.
    Metrics::instance().increment("ops.requests");

    // ─── Auth routes (no token required) ──────────────────────
    if (req.method == "POST" && req.path == "/api/v1/auth/login") {
        return handle_auth_login(req);
    }
    if (req.method == "GET" && req.path == "/api/v1/auth/me") {
        return handle_auth_me(req);
    }

    // ─── Order API routes (#100, #101) ────────────────────────

    // POST /api/v1/orders — submit new order
    if (req.method == "POST" && req.path == "/api/v1/orders") {
        return handle_submit_order(req);
    }

    // POST /api/v1/orders/{id}/cancel — cancel order
    if (req.method == "POST" && req.path.rfind("/api/v1/orders/", 0) == 0 &&
        req.path.find("/cancel") != std::string::npos) {
        auto start = std::string("/api/v1/orders/").size();
        auto end = req.path.find("/cancel");
        auto order_id = static_cast<engine::OrderId>(std::stoull(req.path.substr(start, end - start)));
        return handle_cancel_order(order_id, req);
    }

    // GET /api/v1/orders?participant_id=X&limit=N — order history
    if (req.method == "GET" && req.path == "/api/v1/orders") {
        return handle_query_orders(req);
    }

    // GET /api/v1/orders/{id} — single order detail
    if (req.method == "GET" && req.path.rfind("/api/v1/orders/", 0) == 0 &&
        req.path.find("/cancel") == std::string::npos) {
        auto id_str = req.path.substr(std::string("/api/v1/orders/").size());
        auto order_id = static_cast<engine::OrderId>(std::stoull(id_str));
        return handle_query_order_detail(order_id);
    }

    // GET /api/v1/trades?participant_id=X&limit=N or instrument_id=X&limit=N
    if (req.method == "GET" && req.path == "/api/v1/trades") {
        return handle_query_trades(req);
    }

    // GET /api/v1/instruments/{id}/book — L2 depth
    if (req.method == "GET" && req.path.rfind("/api/v1/instruments/", 0) == 0 &&
        req.path.find("/book") != std::string::npos) {
        auto start = 20; // length of "/api/v1/instruments/"
        auto end = req.path.find("/book");
        auto id = static_cast<engine::InstrumentId>(std::stoul(req.path.substr(start, end - start)));
        return handle_instrument_book(id);
    }

    // GET /api/v1/instruments/{id}/trades — recent trades for instrument
    if (req.method == "GET" && req.path.rfind("/api/v1/instruments/", 0) == 0 &&
        req.path.find("/trades") != std::string::npos) {
        auto start = 20;
        auto end = req.path.find("/trades");
        auto id = static_cast<engine::InstrumentId>(std::stoul(req.path.substr(start, end - start)));
        return handle_instrument_trades(id, req);
    }

    // ─── Public read-only routes ──────────────────────────────
    if (req.method == "GET" && req.path == "/api/v1/metrics") {
        return {200, Metrics::instance().to_prometheus()};
    }
    if (req.method == "GET" && req.path == "/api/v1/metrics/json") {
        return OpsResponse::ok(Metrics::instance().to_json());
    }
    if (req.method == "GET" && req.path == "/api/v1/health") {
        return handle_health();
    }
    if (req.method == "GET" && req.path == "/api/v1/instruments") {
        return handle_instruments();
    }
    if (req.method == "GET" && req.path.rfind("/api/v1/instruments/", 0) == 0 &&
        req.path.find("/halt") == std::string::npos &&
        req.path.find("/resume") == std::string::npos &&
        req.path.find("/close") == std::string::npos &&
        req.path.find("/book") == std::string::npos &&
        req.path.find("/trades") == std::string::npos) {
        auto id_str = req.path.substr(20);
        auto id = static_cast<engine::InstrumentId>(std::stoul(id_str));
        return handle_instrument_detail(id);
    }

    auto user = extract_user(req);

    // ─── Instrument mutation routes (require operator role) ───
    if (req.method == "POST" && req.path.find("/halt") != std::string::npos) {
        if (!require_role(req, "operator")) {
            return OpsResponse::forbidden("operator role required");
        }
        auto start = 20;
        auto end = req.path.find("/halt");
        auto id = static_cast<engine::InstrumentId>(std::stoul(req.path.substr(start, end - start)));
        return handle_instrument_halt(id, user);
    }
    if (req.method == "POST" && req.path.find("/resume") != std::string::npos) {
        if (!require_role(req, "operator")) {
            return OpsResponse::forbidden("operator role required");
        }
        auto start = 20;
        auto end = req.path.find("/resume");
        auto id = static_cast<engine::InstrumentId>(std::stoul(req.path.substr(start, end - start)));
        return handle_instrument_resume(id, user);
    }
    if (req.method == "POST" && req.path.find("/close") != std::string::npos) {
        if (!require_role(req, "operator")) {
            return OpsResponse::forbidden("operator role required");
        }
        auto start = 20;
        auto end = req.path.find("/close");
        auto id = static_cast<engine::InstrumentId>(std::stoul(req.path.substr(start, end - start)));
        return handle_instrument_close(id, user);
    }

    if (req.method == "GET" && req.path == "/api/v1/partitions") {
        return handle_partitions();
    }
    if (req.method == "POST" && req.path == "/api/v1/eod/trigger") {
        if (!require_role(req, "operator")) {
            return OpsResponse::forbidden("operator role required");
        }
        return handle_eod_trigger(user);
    }
    if (req.method == "GET" && req.path == "/api/v1/eod/status") {
        return handle_eod_status();
    }

    // ─── Kill switch routes ───────────────────────────────────
    if (req.method == "GET" && req.path == "/api/v1/kill-switch") {
        return handle_kill_switch_status();
    }
    if (req.method == "POST" && req.path.rfind("/api/v1/kill-switch/", 0) == 0 &&
        req.path.find("/activate") != std::string::npos) {
        if (!require_role(req, "operator")) {
            return OpsResponse::forbidden("operator role required");
        }
        // /api/v1/kill-switch/{pid}/activate
        auto start = std::string("/api/v1/kill-switch/").size();
        auto end = req.path.find("/activate");
        auto pid = static_cast<engine::ParticipantId>(std::stoul(req.path.substr(start, end - start)));
        return handle_kill_switch_activate(pid, user);
    }
    if (req.method == "POST" && req.path.rfind("/api/v1/kill-switch/", 0) == 0 &&
        req.path.find("/deactivate") != std::string::npos) {
        if (!require_role(req, "operator")) {
            return OpsResponse::forbidden("operator role required");
        }
        auto start = std::string("/api/v1/kill-switch/").size();
        auto end = req.path.find("/deactivate");
        auto pid = static_cast<engine::ParticipantId>(std::stoul(req.path.substr(start, end - start)));
        return handle_kill_switch_deactivate(pid, user);
    }

    // ─── FIX session routes ───────────────────────────────────
    if (req.method == "GET" && req.path == "/api/v1/fix/sessions") {
        return handle_fix_sessions();
    }

    // ─── Session config routes ───────────────────────────────
    // GET /api/v1/sessions/active — live connected sessions
    if (req.method == "GET" && req.path == "/api/v1/sessions/active") {
        return handle_sessions_active();
    }

    // GET /api/v1/sessions/config — list all configured sessions
    if (req.method == "GET" && req.path == "/api/v1/sessions/config") {
        return handle_session_config_list();
    }

    // POST /api/v1/sessions/config — add new session config
    if (req.method == "POST" && req.path == "/api/v1/sessions/config") {
        if (!require_role(req, "admin")) {
            return OpsResponse::forbidden("admin role required");
        }
        return handle_session_config_add(req);
    }

    // Routes with {compid} path param
    if (req.path.rfind("/api/v1/sessions/config/", 0) == 0 && req.path.size() > 24) {
        // Extract comp_id — everything between /api/v1/sessions/config/ and the next /
        std::string rest = req.path.substr(24);  // after "/api/v1/sessions/config/"
        auto slash = rest.find('/');
        std::string comp_id = (slash != std::string::npos) ? rest.substr(0, slash) : rest;
        std::string suffix = (slash != std::string::npos) ? rest.substr(slash) : "";

        if (req.method == "GET" && suffix.empty()) {
            return handle_session_config_get(comp_id);
        }
        if (req.method == "PUT" && suffix.empty()) {
            if (!require_role(req, "admin")) {
                return OpsResponse::forbidden("admin role required");
            }
            return handle_session_config_update(comp_id, req);
        }
        if (req.method == "DELETE" && suffix.empty()) {
            if (!require_role(req, "admin")) {
                return OpsResponse::forbidden("admin role required");
            }
            return handle_session_config_delete(comp_id, user);
        }
        if (req.method == "POST" && suffix == "/enable") {
            if (!require_role(req, "admin")) {
                return OpsResponse::forbidden("admin role required");
            }
            return handle_session_config_enable(comp_id, user);
        }
        if (req.method == "POST" && suffix == "/disable") {
            if (!require_role(req, "admin")) {
                return OpsResponse::forbidden("admin role required");
            }
            return handle_session_config_disable(comp_id, user);
        }
        if (req.method == "POST" && suffix == "/reset-seq") {
            if (!require_role(req, "admin")) {
                return OpsResponse::forbidden("admin role required");
            }
            return handle_session_config_reset_seq(comp_id, user);
        }
    }

    // ─── Integration API routes (credit, positions, settlement, reconciliation) ──

    // POST /api/v1/accounts/{pid}/credit-limit
    if (req.method == "POST" && req.path.rfind("/api/v1/accounts/", 0) == 0 &&
        req.path.find("/credit-limit") != std::string::npos &&
        req.path.find("/reset") == std::string::npos) {
        auto start = std::string("/api/v1/accounts/").size();
        auto end = req.path.find("/credit-limit");
        auto pid = static_cast<engine::ParticipantId>(std::stoul(req.path.substr(start, end - start)));
        return handle_set_credit_limit(pid, req);
    }

    // GET /api/v1/accounts/{pid}/credit
    if (req.method == "GET" && req.path.rfind("/api/v1/accounts/", 0) == 0 &&
        req.path.find("/credit") != std::string::npos &&
        req.path.find("/credit-limit") == std::string::npos) {
        auto start = std::string("/api/v1/accounts/").size();
        auto end = req.path.find("/credit");
        auto pid = static_cast<engine::ParticipantId>(std::stoul(req.path.substr(start, end - start)));
        return handle_get_credit(pid);
    }

    // POST /api/v1/accounts/{pid}/credit-limit/reset
    if (req.method == "POST" && req.path.rfind("/api/v1/accounts/", 0) == 0 &&
        req.path.find("/credit-limit/reset") != std::string::npos) {
        auto start = std::string("/api/v1/accounts/").size();
        auto end = req.path.find("/credit-limit/reset");
        auto pid = static_cast<engine::ParticipantId>(std::stoul(req.path.substr(start, end - start)));
        return handle_reset_credit_exposure(pid, user);
    }

    // GET /api/v1/accounts/{pid}/positions/{iid}
    if (req.method == "GET" && req.path.rfind("/api/v1/accounts/", 0) == 0 &&
        req.path.find("/positions/") != std::string::npos) {
        auto acct_start = std::string("/api/v1/accounts/").size();
        auto pos_start = req.path.find("/positions/");
        auto pid = static_cast<engine::ParticipantId>(std::stoul(req.path.substr(acct_start, pos_start - acct_start)));
        auto iid_str = req.path.substr(pos_start + std::string("/positions/").size());
        auto iid = static_cast<engine::InstrumentId>(std::stoul(iid_str));
        return handle_get_position_detail(pid, iid);
    }

    // GET /api/v1/accounts/{pid}/positions
    if (req.method == "GET" && req.path.rfind("/api/v1/accounts/", 0) == 0 &&
        req.path.find("/positions") != std::string::npos &&
        req.path.find("/reset") == std::string::npos) {
        auto start = std::string("/api/v1/accounts/").size();
        auto end = req.path.find("/positions");
        auto pid = static_cast<engine::ParticipantId>(std::stoul(req.path.substr(start, end - start)));
        return handle_get_positions(pid);
    }

    // POST /api/v1/accounts/{pid}/positions/reset
    if (req.method == "POST" && req.path.rfind("/api/v1/accounts/", 0) == 0 &&
        req.path.find("/positions/reset") != std::string::npos) {
        auto start = std::string("/api/v1/accounts/").size();
        auto end = req.path.find("/positions/reset");
        auto pid = static_cast<engine::ParticipantId>(std::stoul(req.path.substr(start, end - start)));
        return handle_reset_positions(pid, user);
    }

    // POST /api/v1/settlement/notify
    if (req.method == "POST" && req.path == "/api/v1/settlement/notify") {
        return handle_settlement_notify(req);
    }

    // GET /api/v1/settlement/status — return stored settlement events
    if (req.method == "GET" && req.path == "/api/v1/settlement/status") {
        return handle_settlement_status();
    }

    // POST /api/v1/settlement/adjust-credit
    if (req.method == "POST" && req.path == "/api/v1/settlement/adjust-credit") {
        return handle_settlement_adjust_credit(req);
    }

    // GET /api/v1/reconciliation/trades
    if (req.method == "GET" && req.path == "/api/v1/reconciliation/trades") {
        return handle_reconciliation_trades(req);
    }

    // GET /api/v1/reconciliation/positions
    if (req.method == "GET" && req.path == "/api/v1/reconciliation/positions") {
        return handle_reconciliation_positions();
    }

    // ─── Surveillance alerts route ─────────────────────────────
    if (req.method == "GET" && req.path == "/api/v1/surveillance/alerts") {
        return handle_surveillance_alerts(req);
    }

    // ─── Trade audit trail route ─────────────────────────────
    if (req.method == "GET" && req.path == "/api/v1/audit/trades") {
        return handle_trade_audit(req);
    }

    // ─── Audit log route ─────────────────────────────────────
    if (req.method == "GET" && req.path == "/api/v1/audit") {
        if (!audit_log_) return OpsResponse::ok("[]");
        return OpsResponse::ok(audit_log_->read_recent(100));
    }

    return OpsResponse::not_found();
}

// ─── Health & instruments (unchanged logic) ──────────────────

OpsResponse OpsController::handle_health() {
    std::ostringstream oss;
    oss << "{\"status\":\"ok\",\"partitions\":" << partitions_.size()
        << ",\"total_instruments\":";
    uint32_t total = 0;
    for (auto* p : partitions_) total += p->instrument_count();
    oss << total << "}";
    return OpsResponse::ok(oss.str());
}

OpsResponse OpsController::handle_instruments() {
    std::ostringstream oss;
    oss << "[";
    bool first = true;
    for (auto* part : partitions_) {
        for (const auto& [id, engine] : part->engines()) {
            if (!first) oss << ",";
            first = false;
            InstrumentInfo info{};
            info.id = id;
            info.state = static_cast<uint8_t>(engine.state());
            info.best_bid = engine.book().best_bid();
            info.best_ask = engine.book().best_ask();
            info.order_count = engine.book().order_count();
            oss << instrument_to_json(info);
        }
    }
    oss << "]";
    return OpsResponse::ok(oss.str());
}

OpsResponse OpsController::handle_instrument_detail(engine::InstrumentId id) {
    for (auto* part : partitions_) {
        auto* eng = part->get_engine(id);
        if (eng) {
            InstrumentInfo info{};
            info.id = id;
            info.state = static_cast<uint8_t>(eng->state());
            info.best_bid = eng->book().best_bid();
            info.best_ask = eng->book().best_ask();
            info.order_count = eng->book().order_count();
            return OpsResponse::ok(instrument_to_json(info));
        }
    }
    return OpsResponse::not_found();
}

OpsResponse OpsController::handle_instrument_halt(engine::InstrumentId id, const std::string& user) {
    auto* part = find_partition_for_instrument(id);
    if (!part) return OpsResponse::not_found();

    auto* eng = part->get_engine_mut(id);
    if (!eng) return OpsResponse::not_found();

    bool ok = eng->transition_state(engine::InstrumentState::Halted);
    if (!ok) {
        return OpsResponse::bad_request("Invalid state transition to Halted from current state");
    }
    Metrics::instance().increment("ops.halt");
    audit(user, "halt", "instrument:" + std::to_string(id));
    return OpsResponse::ok("{\"action\":\"halt\",\"instrument_id\":" +
                           std::to_string(id) + ",\"state\":\"halted\",\"status\":\"executed\"}");
}

OpsResponse OpsController::handle_instrument_resume(engine::InstrumentId id, const std::string& user) {
    auto* part = find_partition_for_instrument(id);
    if (!part) return OpsResponse::not_found();

    auto* eng = part->get_engine_mut(id);
    if (!eng) return OpsResponse::not_found();

    bool ok = eng->transition_state(engine::InstrumentState::Continuous);
    if (!ok) {
        return OpsResponse::bad_request("Invalid state transition to Continuous from current state");
    }
    Metrics::instance().increment("ops.resume");
    audit(user, "resume", "instrument:" + std::to_string(id));
    return OpsResponse::ok("{\"action\":\"resume\",\"instrument_id\":" +
                           std::to_string(id) + ",\"state\":\"continuous\",\"status\":\"executed\"}");
}

OpsResponse OpsController::handle_instrument_close(engine::InstrumentId id, const std::string& user) {
    auto* part = find_partition_for_instrument(id);
    if (!part) return OpsResponse::not_found();

    auto* eng = part->get_engine_mut(id);
    if (!eng) return OpsResponse::not_found();

    bool ok = eng->transition_state(engine::InstrumentState::Closed);
    if (!ok) {
        return OpsResponse::bad_request("Invalid state transition to Closed from current state");
    }
    Metrics::instance().increment("ops.close");
    audit(user, "close", "instrument:" + std::to_string(id));
    return OpsResponse::ok("{\"action\":\"close\",\"instrument_id\":" +
                           std::to_string(id) + ",\"state\":\"closed\",\"status\":\"executed\"}");
}

OpsResponse OpsController::handle_partitions() {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < partitions_.size(); ++i) {
        if (i > 0) oss << ",";
        auto* p = partitions_[i];
        oss << "{\"partition_id\":" << p->id()
            << ",\"sequence\":" << p->sequence()
            << ",\"instruments\":" << p->instrument_count()
            << "}";
    }
    oss << "]";
    return OpsResponse::ok(oss.str());
}

OpsResponse OpsController::handle_eod_trigger(const std::string& user) {
    if (!eod_processor_) {
        return OpsResponse::bad_request("EOD processor not configured");
    }

    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_r(&time_t_now, &tm_buf);
    std::ostringstream date_oss;
    date_oss << std::put_time(&tm_buf, "%Y-%m-%d");
    std::string date_str = date_oss.str();

    auto report = eod_processor_->generate_report(date_str);

    bool write_ok = true;
    if (!eod_output_dir_.empty()) {
        write_ok = eod::EodFileWriter::write_report(report, eod_output_dir_);
    }

    Metrics::instance().increment("ops.eod_trigger");
    audit(user, "eod_trigger", "eod", "date=" + date_str);

    std::ostringstream oss;
    oss << "{\"action\":\"eod_trigger\",\"date\":\"" << date_str
        << "\",\"total_trades\":" << report.total_trades
        << ",\"total_volume\":" << report.total_volume
        << ",\"files_written\":" << (write_ok ? "true" : "false")
        << ",\"status\":\"executed\"}";
    last_eod_result_ = oss.str();
    return OpsResponse::ok(last_eod_result_);
}

OpsResponse OpsController::handle_eod_status() {
    if (last_eod_result_.empty()) {
        return OpsResponse::ok(R"({"status":"no_eod_run"})");
    }
    return OpsResponse::ok(last_eod_result_);
}

// ─── Auth routes ──────────────────────────────────────────────

bool OpsController::check_login_rate_limit() {
    time_t now = std::time(nullptr);
    if (now - login_window_start_ >= RATE_LIMIT_WINDOW_SECONDS) {
        // Reset window
        login_window_start_ = now;
        login_attempt_count_ = 1;
        return true;
    }
    ++login_attempt_count_;
    return login_attempt_count_ <= MAX_LOGIN_ATTEMPTS;
}

OpsResponse OpsController::handle_auth_login(const OpsRequest& req) {
    if (!auth_service_) {
        return OpsResponse::internal_error("auth service not configured");
    }

    // Rate limit check
    if (!check_login_rate_limit()) {
        return OpsResponse::too_many_requests("too many login attempts, try again later");
    }

    // Parse JSON body: {"username":"x","password":"y"}
    // Simple parsing — production would use a JSON library.
    auto extract = [&](const std::string& key) -> std::string {
        auto key_pos = req.body.find("\"" + key + "\"");
        if (key_pos == std::string::npos) return {};
        auto colon = req.body.find(':', key_pos);
        if (colon == std::string::npos) return {};
        auto quote_start = req.body.find('"', colon);
        if (quote_start == std::string::npos) return {};
        auto quote_end = req.body.find('"', quote_start + 1);
        if (quote_end == std::string::npos) return {};
        return req.body.substr(quote_start + 1, quote_end - quote_start - 1);
    };

    std::string username = extract("username");
    std::string password = extract("password");

    if (username.empty() || password.empty()) {
        return OpsResponse::bad_request("missing username or password");
    }

    auto token = auth_service_->login(username, password);
    if (!token) {
        return OpsResponse::unauthorized("invalid credentials");
    }

    audit(username, "login", "auth");
    return OpsResponse::ok("{\"token\":\"" + *token + "\"}");
}

OpsResponse OpsController::handle_auth_me(const OpsRequest& req) {
    if (!auth_service_) {
        return OpsResponse::internal_error("auth service not configured");
    }

    auto token = extract_token(req);
    if (token.empty()) {
        return OpsResponse::unauthorized("missing token");
    }

    auto info = auth_service_->validate(token);
    if (!info) {
        return OpsResponse::unauthorized("invalid token");
    }

    return OpsResponse::ok("{\"username\":\"" + info->username + "\",\"role\":\"" + info->role + "\"}");
}

// ─── Kill switch routes ───────────────────────────────────────

OpsResponse OpsController::handle_kill_switch_status() {
    if (!kill_switch_) {
        return OpsResponse::bad_request("kill switch not configured");
    }

    // Collect blocked participant IDs by scanning a reasonable range.
    // In production, KillSwitchController would expose its active set directly.
    std::ostringstream oss;
    oss << "{\"blocked_participants\":[";
    bool first = true;
    // Scan participant IDs 1..9999 — sufficient for prototype.
    for (engine::ParticipantId pid = 1; pid <= 9999; ++pid) {
        if (kill_switch_->is_active(pid)) {
            if (!first) oss << ",";
            first = false;
            oss << pid;
        }
    }
    oss << "]}";
    return OpsResponse::ok(oss.str());
}

OpsResponse OpsController::handle_kill_switch_activate(engine::ParticipantId pid, const std::string& user) {
    if (!kill_switch_) {
        return OpsResponse::bad_request("kill switch not configured");
    }

    auto cancelled = kill_switch_->activate(pid);
    Metrics::instance().increment("ops.kill_switch_activate");
    audit(user, "kill_switch_activate", "participant:" + std::to_string(pid),
          "cancelled_orders=" + std::to_string(cancelled));

    return OpsResponse::ok("{\"action\":\"activated\",\"participant_id\":" +
                           std::to_string(pid) + ",\"cancelled_orders\":" +
                           std::to_string(cancelled) + "}");
}

OpsResponse OpsController::handle_kill_switch_deactivate(engine::ParticipantId pid, const std::string& user) {
    if (!kill_switch_) {
        return OpsResponse::bad_request("kill switch not configured");
    }

    kill_switch_->deactivate(pid);
    Metrics::instance().increment("ops.kill_switch_deactivate");
    audit(user, "kill_switch_deactivate", "participant:" + std::to_string(pid));

    return OpsResponse::ok("{\"action\":\"deactivated\",\"participant_id\":" +
                           std::to_string(pid) + "}");
}

// ─── FIX session routes ───────────────────────────────────────

OpsResponse OpsController::handle_fix_sessions() {
    if (!session_query_) {
        return OpsResponse::ok("[]");
    }
    return OpsResponse::ok(session_query_());
}

// ─── Session config routes ────────────────────────────────────

OpsResponse OpsController::handle_session_config_list() {
    if (!session_config_store_) {
        return OpsResponse::ok("[]");
    }
    auto sessions = session_config_store_->list_sessions();
    std::ostringstream oss;
    oss << "[";
    bool first = true;
    for (const auto& cfg : sessions) {
        if (!first) oss << ",";
        first = false;
        oss << "{\"sender_comp_id\":\"" << cfg.sender_comp_id
            << "\",\"target_comp_id\":\"" << cfg.target_comp_id
            << "\",\"fix_version\":\"" << cfg.fix_version
            << "\",\"heartbeat_interval\":" << cfg.heartbeat_interval
            << ",\"enabled\":" << (cfg.enabled ? "true" : "false")
            << ",\"participant_id\":" << cfg.participant_id
            << ",\"description\":\"" << cfg.description
            << "\",\"connected\":" << (cfg.connected ? "true" : "false")
            << ",\"inbound_seq\":" << cfg.inbound_seq
            << ",\"outbound_seq\":" << cfg.outbound_seq
            << ",\"last_logon_time\":\"" << cfg.last_logon_time
            << "\",\"last_logout_time\":\"" << cfg.last_logout_time
            << "\"}";
    }
    oss << "]";
    return OpsResponse::ok(oss.str());
}

OpsResponse OpsController::handle_session_config_get(const std::string& comp_id) {
    if (!session_config_store_) {
        return OpsResponse::not_found();
    }
    auto cfg = session_config_store_->get_session(comp_id);
    if (!cfg) return OpsResponse::not_found();

    std::ostringstream oss;
    oss << "{\"sender_comp_id\":\"" << cfg->sender_comp_id
        << "\",\"target_comp_id\":\"" << cfg->target_comp_id
        << "\",\"fix_version\":\"" << cfg->fix_version
        << "\",\"heartbeat_interval\":" << cfg->heartbeat_interval
        << ",\"enabled\":" << (cfg->enabled ? "true" : "false")
        << ",\"participant_id\":" << cfg->participant_id
        << ",\"description\":\"" << cfg->description
        << "\",\"connected\":" << (cfg->connected ? "true" : "false")
        << ",\"inbound_seq\":" << cfg->inbound_seq
        << ",\"outbound_seq\":" << cfg->outbound_seq
        << ",\"last_logon_time\":\"" << cfg->last_logon_time
        << "\",\"last_logout_time\":\"" << cfg->last_logout_time
        << "\"}";
    return OpsResponse::ok(oss.str());
}

OpsResponse OpsController::handle_session_config_add(const OpsRequest& req) {
    if (!session_config_store_) {
        return OpsResponse::bad_request("session config store not configured");
    }

    // Simple JSON extraction
    auto extract_str = [&](const std::string& key) -> std::string {
        auto key_pos = req.body.find("\"" + key + "\"");
        if (key_pos == std::string::npos) return {};
        auto colon = req.body.find(':', key_pos);
        if (colon == std::string::npos) return {};
        auto q1 = req.body.find('"', colon);
        if (q1 == std::string::npos) return {};
        auto q2 = req.body.find('"', q1 + 1);
        if (q2 == std::string::npos) return {};
        return req.body.substr(q1 + 1, q2 - q1 - 1);
    };
    auto extract_num = [&](const std::string& key) -> int64_t {
        auto key_pos = req.body.find("\"" + key + "\"");
        if (key_pos == std::string::npos) return 0;
        auto colon = req.body.find(':', key_pos);
        if (colon == std::string::npos) return 0;
        auto start = colon + 1;
        while (start < req.body.size() && (req.body[start] == ' ' || req.body[start] == '\t')) ++start;
        try { return std::stoll(req.body.substr(start)); } catch (...) { return 0; }
    };
    auto extract_bool = [&](const std::string& key) -> bool {
        auto key_pos = req.body.find("\"" + key + "\"");
        if (key_pos == std::string::npos) return false;
        auto colon = req.body.find(':', key_pos);
        if (colon == std::string::npos) return false;
        auto rest = req.body.substr(colon + 1);
        size_t i = 0;
        while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
        return rest.substr(i, 4) == "true";
    };

    SessionConfig cfg;
    cfg.sender_comp_id = extract_str("sender_comp_id");
    if (cfg.sender_comp_id.empty()) {
        return OpsResponse::bad_request("sender_comp_id is required");
    }
    cfg.target_comp_id = extract_str("target_comp_id");
    if (cfg.target_comp_id.empty()) cfg.target_comp_id = "EXCHANGE";
    cfg.fix_version = extract_str("fix_version");
    if (cfg.fix_version.empty()) cfg.fix_version = "FIX.4.2";
    auto hb = extract_num("heartbeat_interval");
    cfg.heartbeat_interval = hb > 0 ? static_cast<uint32_t>(hb) : 30;
    cfg.enabled = extract_bool("enabled") || req.body.find("\"enabled\"") == std::string::npos;
    cfg.participant_id = static_cast<engine::ParticipantId>(extract_num("participant_id"));
    cfg.description = extract_str("description");

    session_config_store_->add_session(cfg);
    session_config_store_->save();

    auto user = extract_user(req);
    audit(user, "session_config_add", "session:" + cfg.sender_comp_id);

    return OpsResponse::created("{\"status\":\"created\",\"sender_comp_id\":\"" + cfg.sender_comp_id + "\"}");
}

OpsResponse OpsController::handle_session_config_update(const std::string& comp_id, const OpsRequest& req) {
    if (!session_config_store_) {
        return OpsResponse::bad_request("session config store not configured");
    }

    auto existing = session_config_store_->get_session(comp_id);
    if (!existing) return OpsResponse::not_found();

    // Same extraction helpers
    auto extract_str = [&](const std::string& key) -> std::string {
        auto key_pos = req.body.find("\"" + key + "\"");
        if (key_pos == std::string::npos) return {};
        auto colon = req.body.find(':', key_pos);
        if (colon == std::string::npos) return {};
        auto q1 = req.body.find('"', colon);
        if (q1 == std::string::npos) return {};
        auto q2 = req.body.find('"', q1 + 1);
        if (q2 == std::string::npos) return {};
        return req.body.substr(q1 + 1, q2 - q1 - 1);
    };
    auto extract_num = [&](const std::string& key) -> int64_t {
        auto key_pos = req.body.find("\"" + key + "\"");
        if (key_pos == std::string::npos) return -1;
        auto colon = req.body.find(':', key_pos);
        if (colon == std::string::npos) return -1;
        auto start = colon + 1;
        while (start < req.body.size() && (req.body[start] == ' ' || req.body[start] == '\t')) ++start;
        try { return std::stoll(req.body.substr(start)); } catch (...) { return -1; }
    };
    auto extract_bool = [&](const std::string& key, bool default_val) -> bool {
        if (req.body.find("\"" + key + "\"") == std::string::npos) return default_val;
        auto key_pos = req.body.find("\"" + key + "\"");
        auto colon = req.body.find(':', key_pos);
        if (colon == std::string::npos) return default_val;
        auto rest = req.body.substr(colon + 1);
        size_t i = 0;
        while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
        return rest.substr(i, 4) == "true";
    };

    SessionConfig cfg = *existing;
    auto sender = extract_str("sender_comp_id");
    if (!sender.empty()) cfg.sender_comp_id = sender;
    auto target = extract_str("target_comp_id");
    if (!target.empty()) cfg.target_comp_id = target;
    auto fix_ver = extract_str("fix_version");
    if (!fix_ver.empty()) cfg.fix_version = fix_ver;
    auto hb = extract_num("heartbeat_interval");
    if (hb > 0) cfg.heartbeat_interval = static_cast<uint32_t>(hb);
    cfg.enabled = extract_bool("enabled", cfg.enabled);
    auto pid = extract_num("participant_id");
    if (pid >= 0) cfg.participant_id = static_cast<engine::ParticipantId>(pid);
    auto desc = extract_str("description");
    if (!desc.empty()) cfg.description = desc;

    session_config_store_->update_session(comp_id, cfg);
    session_config_store_->save();

    auto user = extract_user(req);
    audit(user, "session_config_update", "session:" + comp_id);

    return OpsResponse::ok("{\"status\":\"updated\",\"sender_comp_id\":\"" + cfg.sender_comp_id + "\"}");
}

OpsResponse OpsController::handle_session_config_delete(const std::string& comp_id, const std::string& user) {
    if (!session_config_store_) {
        return OpsResponse::bad_request("session config store not configured");
    }
    if (!session_config_store_->remove_session(comp_id)) {
        return OpsResponse::not_found();
    }
    session_config_store_->save();
    audit(user, "session_config_delete", "session:" + comp_id);
    return OpsResponse::ok("{\"status\":\"deleted\",\"sender_comp_id\":\"" + comp_id + "\"}");
}

OpsResponse OpsController::handle_session_config_enable(const std::string& comp_id, const std::string& user) {
    if (!session_config_store_) {
        return OpsResponse::bad_request("session config store not configured");
    }
    if (!session_config_store_->enable_session(comp_id)) {
        return OpsResponse::not_found();
    }
    session_config_store_->save();
    audit(user, "session_config_enable", "session:" + comp_id);
    return OpsResponse::ok("{\"status\":\"enabled\",\"sender_comp_id\":\"" + comp_id + "\"}");
}

OpsResponse OpsController::handle_session_config_disable(const std::string& comp_id, const std::string& user) {
    if (!session_config_store_) {
        return OpsResponse::bad_request("session config store not configured");
    }
    if (!session_config_store_->disable_session(comp_id)) {
        return OpsResponse::not_found();
    }
    session_config_store_->save();
    audit(user, "session_config_disable", "session:" + comp_id);
    return OpsResponse::ok("{\"status\":\"disabled\",\"sender_comp_id\":\"" + comp_id + "\"}");
}

OpsResponse OpsController::handle_session_config_reset_seq(const std::string& comp_id, const std::string& user) {
    if (!session_config_store_) {
        return OpsResponse::bad_request("session config store not configured");
    }
    if (!session_config_store_->reset_sequences(comp_id)) {
        return OpsResponse::not_found();
    }
    session_config_store_->save();
    audit(user, "session_config_reset_seq", "session:" + comp_id);
    return OpsResponse::ok("{\"status\":\"sequences_reset\",\"sender_comp_id\":\"" + comp_id + "\"}");
}

OpsResponse OpsController::handle_sessions_active() {
    // Merge config data with live session data from FIX acceptor
    std::string live_json = session_query_ ? session_query_() : "[]";

    if (!session_config_store_) {
        return OpsResponse::ok(live_json);
    }

    // Return the live sessions JSON as-is (it comes from the FIX acceptor)
    return OpsResponse::ok(live_json);
}

// ─── Order API routes (#100, #101) ────────────────────────────

OpsResponse OpsController::handle_submit_order(const OpsRequest& req) {
    if (partitions_.empty()) {
        return OpsResponse::bad_request("no partitions configured");
    }

    // Simple JSON extraction helper
    auto extract_str = [&](const std::string& key) -> std::string {
        auto key_pos = req.body.find("\"" + key + "\"");
        if (key_pos == std::string::npos) return {};
        auto colon = req.body.find(':', key_pos);
        if (colon == std::string::npos) return {};
        auto quote_start = req.body.find('"', colon);
        if (quote_start == std::string::npos) return {};
        auto quote_end = req.body.find('"', quote_start + 1);
        if (quote_end == std::string::npos) return {};
        return req.body.substr(quote_start + 1, quote_end - quote_start - 1);
    };

    auto extract_num = [&](const std::string& key) -> int64_t {
        auto key_pos = req.body.find("\"" + key + "\"");
        if (key_pos == std::string::npos) return 0;
        auto colon = req.body.find(':', key_pos);
        if (colon == std::string::npos) return 0;
        auto val_start = req.body.find_first_of("-0123456789", colon + 1);
        if (val_start == std::string::npos) return 0;
        auto val_end = req.body.find_first_not_of("-0123456789", val_start);
        return std::stoll(req.body.substr(val_start, val_end - val_start));
    };

    // Parse fields
    std::string side_str = extract_str("side");
    std::string type_str = extract_str("type");
    std::string tif_str = extract_str("tif");
    auto price = static_cast<engine::Price>(extract_num("price"));
    auto quantity = static_cast<engine::Quantity>(extract_num("quantity"));
    auto instrument_id = static_cast<engine::InstrumentId>(extract_num("instrument_id"));
    auto participant_id = static_cast<engine::ParticipantId>(extract_num("participant_id"));

    if (quantity == 0) {
        return OpsResponse::bad_request("missing or invalid quantity");
    }

    // Map side
    engine::Side side = engine::Side::Buy;
    if (side_str == "sell" || side_str == "Sell") {
        side = engine::Side::Sell;
    } else if (side_str != "buy" && side_str != "Buy") {
        return OpsResponse::bad_request("invalid side: must be buy or sell");
    }

    // Map order type
    engine::OrderType order_type = engine::OrderType::Limit;
    if (type_str == "market" || type_str == "Market") {
        order_type = engine::OrderType::Market;
    } else if (type_str == "stop" || type_str == "Stop") {
        order_type = engine::OrderType::Stop;
    } else if (type_str == "stop_limit" || type_str == "StopLimit") {
        order_type = engine::OrderType::StopLimit;
    }

    // Map time-in-force
    engine::TimeInForce tif = engine::TimeInForce::Day;
    if (tif_str == "gtc" || tif_str == "GTC") {
        tif = engine::TimeInForce::GTC;
    } else if (tif_str == "gtd" || tif_str == "GTD") {
        tif = engine::TimeInForce::GTD;
    } else if (tif_str == "ioc" || tif_str == "IOC") {
        tif = engine::TimeInForce::IOC;
    } else if (tif_str == "fok" || tif_str == "FOK") {
        tif = engine::TimeInForce::FOK;
    } else if (tif_str == "AuctionOnly" || tif_str == "auctiononly") {
        tif = engine::TimeInForce::AuctionOnly;
    } else if (tif_str == "MOC" || tif_str == "moc") {
        tif = engine::TimeInForce::MarketOnClose;
    } else if (tif_str == "MOO" || tif_str == "moo") {
        tif = engine::TimeInForce::MarketOnOpen;
    }

    // Extract expiry_time for GTD orders
    auto expiry_time = static_cast<engine::Timestamp>(extract_num("expiry_time"));

    // Find the partition for this instrument
    engine::Partition* target_partition = nullptr;
    if (instrument_id != 0) {
        target_partition = find_partition_for_instrument(instrument_id);
    }
    if (!target_partition) {
        // Default to first partition if no instrument_id specified
        target_partition = partitions_[0];
    }

    // Build PartitionMessage
    engine::PartitionMessage msg{};
    msg.type = engine::PartitionMessage::Type::NewOrder;
    msg.instrument_id = instrument_id;
    msg.side = side;
    msg.order_type = order_type;
    msg.time_in_force = tif;
    msg.expiry_time = expiry_time;
    msg.price = price;
    msg.quantity = quantity;
    msg.participant_id = participant_id;
    msg.timestamp = static_cast<engine::Timestamp>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    auto result = target_partition->process(msg);

    Metrics::instance().increment("ops.order_submit");
    auto user = extract_user(req);
    audit(user, "order_submit", "instrument:" + std::to_string(instrument_id));

    // Build response
    std::ostringstream oss;
    if (result.action == engine::MatchResult::Action::Rejected) {
        oss << "{\"error\":\"order rejected\",\"reject_code\":"
            << static_cast<int>(result.reject_code) << "}";
        return {400, oss.str()};
    }

    oss << "{\"order_id\":" << result.assigned_order_id
        << ",\"status\":\"accepted\""
        << ",\"action\":\"" << (result.action == engine::MatchResult::Action::Filled ? "filled" :
                                result.action == engine::MatchResult::Action::Inserted ? "inserted" :
                                "other") << "\"";
    if (!result.fills.empty()) {
        oss << ",\"fills\":" << result.fills.size();
    }
    oss << "}";
    return OpsResponse::created(oss.str());
}

OpsResponse OpsController::handle_cancel_order(engine::OrderId order_id, const OpsRequest& req) {
    // Search all partitions for this order
    for (auto* part : partitions_) {
        // Build a cancel message
        engine::PartitionMessage msg{};
        msg.type = engine::PartitionMessage::Type::Cancel;
        msg.orig_order_id = order_id;
        // We need instrument_id to route. Search engines for the order.
        for (const auto& [iid, eng] : part->engines()) {
            auto* order = eng.book().find_order(order_id);
            if (order) {
                msg.instrument_id = iid;
                msg.participant_id = order->participant_id;
                auto result = part->process(msg);
                if (result.action == engine::MatchResult::Action::Cancelled) {
                    Metrics::instance().increment("ops.order_cancel");
                    auto user = extract_user(req);
                    audit(user, "order_cancel", "order:" + std::to_string(order_id));
                    return OpsResponse::ok("{\"order_id\":" + std::to_string(order_id) +
                                           ",\"status\":\"cancelled\"}");
                }
                return OpsResponse::bad_request("cancel failed");
            }
        }
    }
    return OpsResponse::not_found();
}

OpsResponse OpsController::handle_query_orders(const OpsRequest& req) {
    if (!trade_store_) {
        return OpsResponse::bad_request("trade store not configured");
    }

    auto pid_it = req.params.find("participant_id");
    if (pid_it == req.params.end()) {
        return OpsResponse::bad_request("participant_id parameter required");
    }
    auto pid = static_cast<engine::ParticipantId>(std::stoul(pid_it->second));

    std::size_t limit = 50;
    auto limit_it = req.params.find("limit");
    if (limit_it != req.params.end()) {
        limit = std::stoul(limit_it->second);
    }

    auto orders = trade_store_->query_orders_by_participant(pid, limit);

    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < orders.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& o = orders[i];
        oss << "{\"order_id\":" << o.order_id
            << ",\"participant_id\":" << o.participant_id
            << ",\"instrument_id\":" << o.instrument_id
            << ",\"side\":" << static_cast<int>(o.side)
            << ",\"order_type\":" << static_cast<int>(o.order_type)
            << ",\"time_in_force\":" << static_cast<int>(o.time_in_force)
            << ",\"price\":" << o.price
            << ",\"quantity\":" << o.quantity
            << ",\"event\":" << static_cast<int>(o.event)
            << ",\"timestamp\":" << o.timestamp
            << "}";
    }
    oss << "]";
    return OpsResponse::ok(oss.str());
}

OpsResponse OpsController::handle_query_order_detail(engine::OrderId order_id) {
    // Search resting orders in all partitions
    for (auto* part : partitions_) {
        for (const auto& [iid, eng] : part->engines()) {
            auto* order = eng.book().find_order(order_id);
            if (order) {
                std::ostringstream oss;
                oss << "{\"order_id\":" << order->order_id
                    << ",\"client_order_id\":" << order->client_order_id
                    << ",\"participant_id\":" << order->participant_id
                    << ",\"instrument_id\":" << order->instrument_id
                    << ",\"side\":" << static_cast<int>(order->side)
                    << ",\"order_type\":" << static_cast<int>(order->order_type)
                    << ",\"price\":" << order->price
                    << ",\"original_qty\":" << order->original_qty
                    << ",\"remaining_qty\":" << order->remaining_qty
                    << ",\"cumulative_filled\":" << order->cumulative_filled
                    << "}";
                return OpsResponse::ok(oss.str());
            }
        }
    }
    return OpsResponse::not_found();
}

OpsResponse OpsController::handle_query_trades(const OpsRequest& req) {
    if (!trade_store_) {
        return OpsResponse::bad_request("trade store not configured");
    }

    std::size_t limit = 50;
    auto limit_it = req.params.find("limit");
    if (limit_it != req.params.end()) {
        limit = std::stoul(limit_it->second);
    }

    std::vector<posttrade::TradeRecord> trades;

    auto pid_it = req.params.find("participant_id");
    auto iid_it = req.params.find("instrument_id");

    if (pid_it != req.params.end()) {
        auto pid = static_cast<engine::ParticipantId>(std::stoul(pid_it->second));
        trades = trade_store_->query_by_participant(pid, limit);
    } else if (iid_it != req.params.end()) {
        auto iid = static_cast<engine::InstrumentId>(std::stoul(iid_it->second));
        trades = trade_store_->query_by_instrument(iid, limit);
    } else {
        return OpsResponse::bad_request("participant_id or instrument_id parameter required");
    }

    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < trades.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& t = trades[i];
        oss << "{\"trade_id\":" << t.trade_id
            << ",\"buy_order_id\":" << t.buy_order_id
            << ",\"sell_order_id\":" << t.sell_order_id
            << ",\"buyer_id\":" << t.buyer_id
            << ",\"seller_id\":" << t.seller_id
            << ",\"instrument_id\":" << t.instrument_id
            << ",\"price\":" << t.price
            << ",\"quantity\":" << t.quantity
            << ",\"timestamp\":" << t.timestamp
            << "}";
    }
    oss << "]";
    return OpsResponse::ok(oss.str());
}

OpsResponse OpsController::handle_instrument_trades(engine::InstrumentId id, const OpsRequest& req) {
    if (!trade_store_) {
        return OpsResponse::bad_request("trade store not configured");
    }

    std::size_t limit = 50;
    auto limit_it = req.params.find("limit");
    if (limit_it != req.params.end()) {
        limit = std::stoul(limit_it->second);
    }

    auto trades = trade_store_->query_by_instrument(id, limit);

    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < trades.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& t = trades[i];
        oss << "{\"trade_id\":" << t.trade_id
            << ",\"price\":" << t.price
            << ",\"quantity\":" << t.quantity
            << ",\"buyer_id\":" << t.buyer_id
            << ",\"seller_id\":" << t.seller_id
            << ",\"timestamp\":" << t.timestamp
            << "}";
    }
    oss << "]";
    return OpsResponse::ok(oss.str());
}

OpsResponse OpsController::handle_instrument_book(engine::InstrumentId id) {
    auto* part = find_partition_for_instrument(id);
    if (!part) return OpsResponse::not_found();

    auto* eng = part->get_engine(id);
    if (!eng) return OpsResponse::not_found();

    // Build L2 depth from the order book
    struct Level {
        engine::Price price;
        engine::Quantity qty;
        uint32_t count;
    };

    std::vector<Level> bids;
    std::vector<Level> asks;

    eng->book().for_each_order([&](const engine::Order& order) {
        auto& side_levels = (order.side == engine::Side::Buy) ? bids : asks;

        // Check if we already have this price level
        bool found = false;
        for (auto& lvl : side_levels) {
            if (lvl.price == order.price) {
                lvl.qty += order.remaining_qty;
                lvl.count++;
                found = true;
                break;
            }
        }
        if (!found) {
            side_levels.push_back({order.price, order.remaining_qty, 1});
        }
    });

    std::ostringstream oss;
    oss << "{\"instrument_id\":" << id;

    oss << ",\"bids\":[";
    for (size_t i = 0; i < bids.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"price\":" << bids[i].price
            << ",\"quantity\":" << bids[i].qty
            << ",\"count\":" << bids[i].count << "}";
    }
    oss << "]";

    oss << ",\"asks\":[";
    for (size_t i = 0; i < asks.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{\"price\":" << asks[i].price
            << ",\"quantity\":" << asks[i].qty
            << ",\"count\":" << asks[i].count << "}";
    }
    oss << "]";

    oss << "}";
    return OpsResponse::ok(oss.str());
}

// ─── Helpers ──────────────────────────────────────────────────

engine::Partition* OpsController::find_partition_for_instrument(engine::InstrumentId id) {
    for (auto* part : partitions_) {
        if (part->get_engine(id) != nullptr) return part;
    }
    return nullptr;
}

std::string OpsController::instrument_to_json(const InstrumentInfo& info) {
    std::ostringstream oss;
    oss << "{\"instrument_id\":" << info.id
        << ",\"state\":" << static_cast<int>(info.state)
        << ",\"best_bid\":" << info.best_bid
        << ",\"best_ask\":" << info.best_ask
        << ",\"order_count\":" << info.order_count
        << "}";
    return oss.str();
}

std::string OpsController::extract_token(const OpsRequest& req) const {
    auto it = req.headers.find("Authorization");
    if (it == req.headers.end()) return {};

    const std::string& value = it->second;
    const std::string prefix = "Bearer ";
    if (value.size() > prefix.size() && value.substr(0, prefix.size()) == prefix) {
        return value.substr(prefix.size());
    }
    return {};
}

std::string OpsController::extract_user(const OpsRequest& req) const {
    if (!auth_service_) return "anonymous";
    auto token = extract_token(req);
    if (token.empty()) return "anonymous";
    auto info = auth_service_->validate(token);
    if (!info) return "anonymous";
    return info->username;
}

bool OpsController::require_role(const OpsRequest& req, const std::string& min_role) const {
    if (!auth_service_) return true;  // No auth = allow all
    auto token = extract_token(req);
    if (token.empty()) return false;
    return auth_service_->has_role(token, min_role);
}

void OpsController::audit(const std::string& user, const std::string& action,
                           const std::string& target, const std::string& details) {
    if (audit_log_) {
        audit_log_->log(user, action, target, details);
    }
}

// ─── Integration API handlers ─────────────────────────────────

OpsResponse OpsController::handle_set_credit_limit(engine::ParticipantId pid, const OpsRequest& req) {
    if (!risk_service_) {
        return OpsResponse::bad_request("risk service not configured");
    }

    auto extract_num = [&](const std::string& key) -> int64_t {
        auto key_pos = req.body.find("\"" + key + "\"");
        if (key_pos == std::string::npos) return -1;
        auto colon = req.body.find(':', key_pos);
        if (colon == std::string::npos) return -1;
        auto start = colon + 1;
        while (start < req.body.size() && (req.body[start] == ' ' || req.body[start] == '\t')) ++start;
        try { return std::stoll(req.body.substr(start)); } catch (...) { return -1; }
    };

    auto max_credit = extract_num("max_credit");
    if (max_credit < 0) {
        return OpsResponse::bad_request("missing or invalid max_credit");
    }

    risk_service_->credit_tracker().set_limit(pid, max_credit);

    auto user = extract_user(req);
    audit(user, "set_credit_limit", "participant:" + std::to_string(pid),
          "max_credit=" + std::to_string(max_credit));
    Metrics::instance().increment("ops.credit_limit_set");

    return OpsResponse::ok("{\"participant_id\":" + std::to_string(pid) +
                           ",\"max_credit\":" + std::to_string(max_credit) + "}");
}

OpsResponse OpsController::handle_get_credit(engine::ParticipantId pid) {
    if (!risk_service_) {
        return OpsResponse::bad_request("risk service not configured");
    }

    auto& tracker = risk_service_->credit_tracker();
    auto max_credit = tracker.get_limit(pid);
    auto exposure = tracker.get_exposure(pid);
    auto available = max_credit - exposure;
    if (available < 0) available = 0;

    std::ostringstream oss;
    oss << "{\"participant_id\":" << pid
        << ",\"max_credit\":" << max_credit
        << ",\"current_exposure\":" << exposure
        << ",\"available\":" << available
        << "}";
    return OpsResponse::ok(oss.str());
}

OpsResponse OpsController::handle_reset_credit_exposure(engine::ParticipantId pid, const std::string& user) {
    if (!risk_service_) {
        return OpsResponse::bad_request("risk service not configured");
    }

    risk_service_->credit_tracker().reset_exposure(pid);

    audit(user, "reset_credit_exposure", "participant:" + std::to_string(pid));
    Metrics::instance().increment("ops.credit_exposure_reset");

    return OpsResponse::ok("{\"participant_id\":" + std::to_string(pid) +
                           ",\"current_exposure\":0,\"status\":\"reset\"}");
}

OpsResponse OpsController::handle_get_positions(engine::ParticipantId pid) {
    if (!risk_service_) {
        return OpsResponse::bad_request("risk service not configured");
    }

    auto positions = risk_service_->position_tracker().get_positions_for_participant(pid);

    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < positions.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& [iid, pos] = positions[i];
        oss << "{\"instrument_id\":" << iid
            << ",\"net_qty\":" << pos.net_qty
            << ",\"buy_qty\":" << pos.buy_qty
            << ",\"sell_qty\":" << pos.sell_qty
            << "}";
    }
    oss << "]";
    return OpsResponse::ok(oss.str());
}

OpsResponse OpsController::handle_get_position_detail(engine::ParticipantId pid, engine::InstrumentId iid) {
    if (!risk_service_) {
        return OpsResponse::bad_request("risk service not configured");
    }

    auto pos = risk_service_->position_tracker().get_position(pid, iid);

    std::ostringstream oss;
    oss << "{\"participant_id\":" << pid
        << ",\"instrument_id\":" << iid
        << ",\"net_qty\":" << pos.net_qty
        << ",\"buy_qty\":" << pos.buy_qty
        << ",\"sell_qty\":" << pos.sell_qty
        << "}";
    return OpsResponse::ok(oss.str());
}

OpsResponse OpsController::handle_reset_positions(engine::ParticipantId pid, const std::string& user) {
    if (!risk_service_) {
        return OpsResponse::bad_request("risk service not configured");
    }

    risk_service_->position_tracker().reset_participant(pid);

    audit(user, "reset_positions", "participant:" + std::to_string(pid));
    Metrics::instance().increment("ops.positions_reset");

    return OpsResponse::ok("{\"participant_id\":" + std::to_string(pid) +
                           ",\"status\":\"positions_reset\"}");
}

OpsResponse OpsController::handle_settlement_notify(const OpsRequest& req) {
    if (!trade_store_) {
        return OpsResponse::bad_request("trade store not configured");
    }

    // Parse trade_ids array and status
    auto extract_str = [&](const std::string& key) -> std::string {
        auto key_pos = req.body.find("\"" + key + "\"");
        if (key_pos == std::string::npos) return {};
        auto colon = req.body.find(':', key_pos);
        if (colon == std::string::npos) return {};
        auto q1 = req.body.find('"', colon);
        if (q1 == std::string::npos) return {};
        auto q2 = req.body.find('"', q1 + 1);
        if (q2 == std::string::npos) return {};
        return req.body.substr(q1 + 1, q2 - q1 - 1);
    };

    // Parse trade_ids array: simple parser for [1,2,3]
    std::vector<engine::TradeId> trade_ids;
    auto arr_start = req.body.find("[");
    auto arr_end = req.body.find("]");
    if (arr_start != std::string::npos && arr_end != std::string::npos && arr_end > arr_start) {
        std::string arr_content = req.body.substr(arr_start + 1, arr_end - arr_start - 1);
        std::istringstream iss(arr_content);
        std::string token;
        while (std::getline(iss, token, ',')) {
            // Trim whitespace
            size_t s = 0;
            while (s < token.size() && (token[s] == ' ' || token[s] == '\t')) ++s;
            if (s < token.size()) {
                try {
                    trade_ids.push_back(static_cast<engine::TradeId>(std::stoull(token.substr(s))));
                } catch (...) {}
            }
        }
    }

    auto status = extract_str("status");
    auto settlement_date = extract_str("settlement_date");

    if (trade_ids.empty()) {
        return OpsResponse::bad_request("trade_ids array required");
    }

    auto user = extract_user(req);
    audit(user, "settlement_notify", "settlement",
          "trade_count=" + std::to_string(trade_ids.size()) +
          " status=" + status + " date=" + settlement_date);
    Metrics::instance().increment("ops.settlement_notify");

    // Store the settlement event for GET /api/v1/settlement/status
    {
        std::ostringstream evt;
        evt << "{\"trade_ids\":[";
        for (size_t i = 0; i < trade_ids.size(); ++i) {
            if (i > 0) evt << ",";
            evt << "\"" << trade_ids[i] << "\"";
        }
        evt << "],\"status\":\"" << (status.empty() ? "Pending" : status) << "\""
            << ",\"settlement_date\":\"" << settlement_date << "\""
            << "}";
        settlement_events_.push_back(evt.str());
    }

    std::ostringstream oss;
    oss << "{\"status\":\"acknowledged\""
        << ",\"trade_count\":" << trade_ids.size()
        << ",\"settlement_status\":\"" << status << "\""
        << ",\"settlement_date\":\"" << settlement_date << "\""
        << "}";
    return OpsResponse::ok(oss.str());
}

OpsResponse OpsController::handle_settlement_adjust_credit(const OpsRequest& req) {
    if (!risk_service_) {
        return OpsResponse::bad_request("risk service not configured");
    }

    auto extract_num = [&](const std::string& key) -> int64_t {
        auto key_pos = req.body.find("\"" + key + "\"");
        if (key_pos == std::string::npos) return 0;
        auto colon = req.body.find(':', key_pos);
        if (colon == std::string::npos) return 0;
        auto start = colon + 1;
        while (start < req.body.size() && (req.body[start] == ' ' || req.body[start] == '\t')) ++start;
        try { return std::stoll(req.body.substr(start)); } catch (...) { return 0; }
    };
    auto extract_str = [&](const std::string& key) -> std::string {
        auto key_pos = req.body.find("\"" + key + "\"");
        if (key_pos == std::string::npos) return {};
        auto colon = req.body.find(':', key_pos);
        if (colon == std::string::npos) return {};
        auto q1 = req.body.find('"', colon);
        if (q1 == std::string::npos) return {};
        auto q2 = req.body.find('"', q1 + 1);
        if (q2 == std::string::npos) return {};
        return req.body.substr(q1 + 1, q2 - q1 - 1);
    };

    auto pid = static_cast<engine::ParticipantId>(extract_num("participant_id"));
    auto adjustment = extract_num("adjustment");
    auto reason = extract_str("reason");

    if (pid == 0) {
        return OpsResponse::bad_request("missing participant_id");
    }

    risk_service_->credit_tracker().adjust_exposure(pid, adjustment);

    auto user = extract_user(req);
    audit(user, "settlement_adjust_credit", "participant:" + std::to_string(pid),
          "adjustment=" + std::to_string(adjustment) + " reason=" + reason);
    Metrics::instance().increment("ops.settlement_credit_adjust");

    auto new_exposure = risk_service_->credit_tracker().get_exposure(pid);
    std::ostringstream oss;
    oss << "{\"participant_id\":" << pid
        << ",\"adjustment\":" << adjustment
        << ",\"new_exposure\":" << new_exposure
        << ",\"reason\":\"" << reason << "\""
        << "}";
    return OpsResponse::ok(oss.str());
}

OpsResponse OpsController::handle_reconciliation_trades(const OpsRequest& req) {
    if (!trade_store_) {
        return OpsResponse::bad_request("trade store not configured");
    }

    // For now, return all trades. In production, filter by date param.
    auto trades = trade_store_->query_all();

    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < trades.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& t = trades[i];
        oss << "{\"trade_id\":" << t.trade_id
            << ",\"buy_order_id\":" << t.buy_order_id
            << ",\"sell_order_id\":" << t.sell_order_id
            << ",\"buyer_id\":" << t.buyer_id
            << ",\"seller_id\":" << t.seller_id
            << ",\"instrument_id\":" << t.instrument_id
            << ",\"price\":" << t.price
            << ",\"quantity\":" << t.quantity
            << ",\"buyer_fee\":" << t.buyer_fee
            << ",\"seller_fee\":" << t.seller_fee
            << ",\"timestamp\":" << t.timestamp
            << "}";
    }
    oss << "]";
    return OpsResponse::ok(oss.str());
}

OpsResponse OpsController::handle_reconciliation_positions() {
    if (!risk_service_) {
        return OpsResponse::bad_request("risk service not configured");
    }

    auto positions = risk_service_->position_tracker().get_all_positions();

    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < positions.size(); ++i) {
        if (i > 0) oss << ",";
        const auto& [pid, iid, pos] = positions[i];
        oss << "{\"participant_id\":" << pid
            << ",\"instrument_id\":" << iid
            << ",\"net_qty\":" << pos.net_qty
            << ",\"buy_qty\":" << pos.buy_qty
            << ",\"sell_qty\":" << pos.sell_qty
            << "}";
    }
    oss << "]";
    return OpsResponse::ok(oss.str());
}

// ─── Settlement status (GET) ─────────────────────────────────

OpsResponse OpsController::handle_settlement_status() {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < settlement_events_.size(); ++i) {
        if (i > 0) oss << ",";
        oss << settlement_events_[i];
    }
    oss << "]";
    return OpsResponse::ok(oss.str());
}

// ─── Surveillance alerts ─────────────────────────────────────

OpsResponse OpsController::handle_surveillance_alerts(const OpsRequest& req) {
    if (!surveillance_engine_) {
        return OpsResponse::ok("[]");
    }

    auto alerts = surveillance_engine_->collect_alerts();

    // Optional severity filter
    std::string filter_severity;
    auto it = req.params.find("severity");
    if (it != req.params.end()) filter_severity = it->second;

    auto severity_str = [](surveillance::AlertSeverity s) -> std::string {
        switch (s) {
            case surveillance::AlertSeverity::Critical: return "Critical";
            case surveillance::AlertSeverity::High:     return "High";
            case surveillance::AlertSeverity::Medium:   return "Medium";
            case surveillance::AlertSeverity::Low:      return "Low";
            default: return "Unknown";
        }
    };

    std::ostringstream oss;
    oss << "[";
    bool first = true;
    for (const auto& a : alerts) {
        auto sev = severity_str(a.severity);
        if (!filter_severity.empty() && sev != filter_severity) continue;
        if (!first) oss << ",";
        first = false;
        oss << "{\"alert_id\":" << a.alert_id
            << ",\"severity\":\"" << sev << "\""
            << ",\"rule\":\"" << a.rule_name << "\""
            << ",\"participant_id\":" << a.participant_id
            << ",\"instrument_id\":" << a.instrument_id
            << ",\"description\":\"" << a.description << "\""
            << ",\"timestamp\":" << a.timestamp
            << "}";
    }
    oss << "]";
    return OpsResponse::ok(oss.str());
}

// ─── Trade audit trail ───────────────────────────────────────

OpsResponse OpsController::handle_trade_audit(const OpsRequest& req) {
    if (!trade_audit_trail_) {
        return OpsResponse::ok("[]");
    }

    // Check for filters
    auto pid_it = req.params.find("participant_id");
    auto iid_it = req.params.find("instrument_id");

    std::vector<AuditEntry> entries;
    if (pid_it != req.params.end()) {
        auto pid = static_cast<engine::ParticipantId>(std::stoul(pid_it->second));
        entries = trade_audit_trail_->read_by_participant(pid, 200);
    } else if (iid_it != req.params.end()) {
        auto iid = static_cast<engine::InstrumentId>(std::stoul(iid_it->second));
        entries = trade_audit_trail_->read_by_instrument(iid, 200);
    } else {
        entries = trade_audit_trail_->read_recent(200);
    }

    // Optional type filter
    std::string filter_type;
    auto type_it = req.params.find("type");
    if (type_it != req.params.end()) filter_type = type_it->second;

    auto type_str = [](AuditEntryType t) -> std::string {
        switch (t) {
            case AuditEntryType::NewOrder:   return "New";
            case AuditEntryType::Cancel:     return "Cancel";
            case AuditEntryType::Fill:       return "Fill";
            case AuditEntryType::Reject:     return "Reject";
            case AuditEntryType::Modify:     return "Modify";
            case AuditEntryType::MassCancel: return "MassCancel";
            default: return "Unknown";
        }
    };
    auto side_str = [](engine::Side s) -> std::string {
        return s == engine::Side::Buy ? "Buy" : "Sell";
    };

    std::ostringstream oss;
    oss << "[";
    bool first = true;
    for (const auto& e : entries) {
        auto tstr = type_str(e.type);
        if (!filter_type.empty() && tstr != filter_type) continue;
        if (!first) oss << ",";
        first = false;
        oss << "{\"sequence\":" << e.sequence
            << ",\"type\":\"" << tstr << "\""
            << ",\"timestamp\":" << e.timestamp
            << ",\"participant_id\":" << e.participant_id
            << ",\"instrument_id\":" << e.instrument_id
            << ",\"order_id\":" << e.order_id
            << ",\"side\":\"" << side_str(e.side) << "\""
            << ",\"price\":" << e.price
            << ",\"quantity\":" << e.quantity
            << ",\"hash\":\"" << e.entry_hash << "\""
            << "}";
    }
    oss << "]";
    return OpsResponse::ok(oss.str());
}

}  // namespace ops
