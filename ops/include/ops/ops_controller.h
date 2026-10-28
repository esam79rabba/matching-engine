#pragma once

#include "engine/types.h"
#include "engine/partition.h"
#include "engine/cancel_on_disconnect.h"
#include "engine/kill_switch.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <ctime>
#include <utility>

namespace posttrade { class TradeStore; }

namespace eod { class EodProcessor; }

namespace risk { class BasicRiskService; }

namespace refdata { class RefDataService; }

namespace surveillance { class SurveillanceEngine; }

namespace ops { class TradeAuditTrail; }

namespace ops {

class AuthService;
class AuditLog;
class SessionConfigStore;

/// Simplified HTTP-like request/response for the ops API.
/// In production this would be backed by a real HTTP framework.
/// For M4 this is a direct-call interface testable without networking.
struct OpsRequest {
    std::string method;   // GET, POST, PUT, DELETE
    std::string path;     // e.g., "/api/v1/health"
    std::string body;     // JSON body (if any)
    std::unordered_map<std::string, std::string> params;  // Query/path params
    std::unordered_map<std::string, std::string> headers;  // HTTP headers
};

struct OpsResponse {
    int         status = 200;
    std::string body;      // JSON response body

    static OpsResponse ok(const std::string& json) { return {200, json}; }
    static OpsResponse created(const std::string& json) { return {201, json}; }
    static OpsResponse bad_request(const std::string& msg) { return {400, "{\"error\":\"" + msg + "\"}"}; }
    static OpsResponse unauthorized(const std::string& msg = "unauthorized") { return {401, "{\"error\":\"" + msg + "\"}"}; }
    static OpsResponse forbidden(const std::string& msg = "forbidden") { return {403, "{\"error\":\"" + msg + "\"}"}; }
    static OpsResponse not_found() { return {404, "{\"error\":\"not found\"}"}; }
    static OpsResponse too_many_requests(const std::string& msg) { return {429, "{\"error\":\"" + msg + "\"}"}; }
    static OpsResponse internal_error(const std::string& msg) { return {500, "{\"error\":\"" + msg + "\"}"}; }
};

/// Information about a single instrument's current state.
struct InstrumentInfo {
    engine::InstrumentId id;
    uint8_t              state;        // InstrumentState enum
    engine::Price        best_bid;
    engine::Price        best_ask;
    uint32_t             order_count;
};

/// Callback type for querying FIX session state as JSON.
using SessionQueryFn = std::function<std::string()>;

/// Operations controller — the control-plane API boundary.
///
/// Translates REST-like requests into engine control actions.
/// Completely separated from the hot path. Does NOT touch engine
/// internals directly — operates via Partition's public API and
/// configuration interfaces.
///
/// Architecture:
///   - NOT on the hot path
///   - May use standard allocations, logging, JSON serialization
///   - Auth via AuthService (JWT-like tokens for portal)
///   - All mutating actions are logged via AuditLog
///
/// Generic — no asset-class-specific logic.
class OpsController {
public:
    /// Register a partition for management. The controller does not own it.
    void register_partition(engine::Partition* partition);

    /// Set EOD processor for triggering end-of-day reports.
    void set_eod_processor(eod::EodProcessor* processor, const std::string& output_dir);

    /// Set auth service for login/validate. Non-owning.
    void set_auth_service(AuthService* svc);

    /// Set kill switch controller. Non-owning.
    void set_kill_switch(engine::KillSwitchController* ks);

    /// Set FIX session query callback.
    void set_session_query(SessionQueryFn fn);

    /// Set audit log. Non-owning.
    void set_audit_log(AuditLog* log);

    /// Set trade store for order/trade queries. Non-owning.
    void set_trade_store(posttrade::TradeStore* store);

    /// Set session config store. Non-owning.
    void set_session_config_store(SessionConfigStore* store);

    /// Set risk service for credit/position queries. Non-owning.
    void set_risk_service(risk::BasicRiskService* svc);

    /// Set ref data service for notional / price-multiplier lookups during risk checks.
    /// Non-owning. If unset, risk checks fall back to zero multiplier.
    void set_ref_data(refdata::RefDataService* svc);

    /// Set surveillance engine for alert queries. Non-owning.
    void set_surveillance_engine(surveillance::SurveillanceEngine* engine);

    /// Set trade audit trail for trade-level audit queries. Non-owning.
    void set_trade_audit_trail(TradeAuditTrail* trail);

    /// Process an ops request and return a response.
    OpsResponse handle(const OpsRequest& req);

private:
    std::vector<engine::Partition*> partitions_;
    eod::EodProcessor* eod_processor_ = nullptr;
    std::string eod_output_dir_;
    std::string last_eod_result_;
    AuthService* auth_service_ = nullptr;
    engine::KillSwitchController* kill_switch_ = nullptr;
    SessionQueryFn session_query_;
    AuditLog* audit_log_ = nullptr;
    posttrade::TradeStore* trade_store_ = nullptr;
    SessionConfigStore* session_config_store_ = nullptr;
    risk::BasicRiskService* risk_service_ = nullptr;
    refdata::RefDataService* ref_data_ = nullptr;
    surveillance::SurveillanceEngine* surveillance_engine_ = nullptr;
    TradeAuditTrail* trade_audit_trail_ = nullptr;
    std::vector<std::string> settlement_events_;  // Stored settlement events as JSON

    // ─── Route handlers ─────────────────────────────────────

    OpsResponse handle_health();
    OpsResponse handle_instruments();
    OpsResponse handle_instrument_detail(engine::InstrumentId id);
    OpsResponse handle_instrument_halt(engine::InstrumentId id, const std::string& user);
    OpsResponse handle_instrument_resume(engine::InstrumentId id, const std::string& user);
    OpsResponse handle_instrument_close(engine::InstrumentId id, const std::string& user);
    OpsResponse handle_partitions();
    OpsResponse handle_partition_detail(engine::PartitionId id);
    OpsResponse handle_eod_trigger(const std::string& user);
    OpsResponse handle_eod_status();

    // ─── Auth routes ──────────────────────────────────────────

    OpsResponse handle_auth_login(const OpsRequest& req);
    OpsResponse handle_auth_me(const OpsRequest& req);

    // ─── Kill switch routes ───────────────────────────────────

    OpsResponse handle_kill_switch_status();
    OpsResponse handle_kill_switch_activate(engine::ParticipantId pid, const std::string& user);
    OpsResponse handle_kill_switch_deactivate(engine::ParticipantId pid, const std::string& user);

    // ─── Order API routes (#100, #101) ─────────────────────────

    OpsResponse handle_submit_order(const OpsRequest& req);
    OpsResponse handle_cancel_order(engine::OrderId order_id, const OpsRequest& req);
    OpsResponse handle_query_orders(const OpsRequest& req);
    OpsResponse handle_query_order_detail(engine::OrderId order_id);
    OpsResponse handle_query_trades(const OpsRequest& req);
    OpsResponse handle_instrument_trades(engine::InstrumentId id, const OpsRequest& req);
    OpsResponse handle_instrument_book(engine::InstrumentId id);

    // ─── FIX session routes ───────────────────────────────────

    OpsResponse handle_fix_sessions();

    // ─── Session config routes ───────────────────────────────

    OpsResponse handle_session_config_list();
    OpsResponse handle_session_config_get(const std::string& comp_id);
    OpsResponse handle_session_config_add(const OpsRequest& req);
    OpsResponse handle_session_config_update(const std::string& comp_id, const OpsRequest& req);
    OpsResponse handle_session_config_delete(const std::string& comp_id, const std::string& user);
    OpsResponse handle_session_config_enable(const std::string& comp_id, const std::string& user);
    OpsResponse handle_session_config_disable(const std::string& comp_id, const std::string& user);
    OpsResponse handle_session_config_reset_seq(const std::string& comp_id, const std::string& user);
    OpsResponse handle_sessions_active();

    // ─── Integration API routes (credit, positions, settlement, reconciliation) ──

    OpsResponse handle_set_credit_limit(engine::ParticipantId pid, const OpsRequest& req);
    OpsResponse handle_get_credit(engine::ParticipantId pid);
    OpsResponse handle_reset_credit_exposure(engine::ParticipantId pid, const std::string& user);
    OpsResponse handle_get_positions(engine::ParticipantId pid);
    OpsResponse handle_get_position_detail(engine::ParticipantId pid, engine::InstrumentId iid);
    OpsResponse handle_reset_positions(engine::ParticipantId pid, const std::string& user);
    OpsResponse handle_settlement_notify(const OpsRequest& req);
    OpsResponse handle_settlement_status();
    OpsResponse handle_settlement_adjust_credit(const OpsRequest& req);
    OpsResponse handle_reconciliation_trades(const OpsRequest& req);
    OpsResponse handle_reconciliation_positions();

    // ─── Surveillance routes ────────────────────────────────────
    OpsResponse handle_surveillance_alerts(const OpsRequest& req);

    // ─── Trade audit trail routes ────────────────────────────────
    OpsResponse handle_trade_audit(const OpsRequest& req);

    // ─── Helpers ────────────────────────────────────────────

    engine::Partition* find_partition_for_instrument(engine::InstrumentId id);
    std::string instrument_to_json(const InstrumentInfo& info);

    /// Extract Bearer token from request headers. Returns empty string if absent.
    std::string extract_token(const OpsRequest& req) const;

    /// Extract username from token (for audit). Returns "anonymous" if no auth.
    std::string extract_user(const OpsRequest& req) const;

    /// Check if the request has at least the given role level.
    /// Returns true if no auth_service_ is configured (permissive mode).
    bool require_role(const OpsRequest& req, const std::string& min_role) const;

    /// Audit helper — no-op if audit_log_ is null.
    void audit(const std::string& user, const std::string& action,
               const std::string& target, const std::string& details = "");

public:
    // ─── Login rate limiter constants ─────────────────────────
    static constexpr int MAX_LOGIN_ATTEMPTS = 10;
    static constexpr int RATE_LIMIT_WINDOW_SECONDS = 60;

private:
    int login_attempt_count_ = 0;
    time_t login_window_start_ = 0;

    /// Check and update the login rate limiter. Returns true if allowed.
    bool check_login_rate_limit();
};

}  // namespace ops
