#include "ops/ops_controller.h"
#include "ops/metrics.h"
#include "ops/auth_service.h"
#include "ops/audit_log.h"
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
        req.path.find("/close") == std::string::npos) {
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

OpsResponse OpsController::handle_auth_login(const OpsRequest& req) {
    if (!auth_service_) {
        return OpsResponse::internal_error("auth service not configured");
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

}  // namespace ops
