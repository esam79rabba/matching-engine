#pragma once

#include "refdata/ref_data_service.h"
#include "engine/partition.h"
#include <string>
#include <unordered_map>
#include <memory>

namespace refdata {

/// Simple HTTP request/response types for ref data service.
/// Same pattern as ops::OpsController — direct-call interface
/// testable without a real HTTP server.
///
/// Production: wrap with a real HTTP server (e.g., httplib, Crow, or Boost.Beast).
/// FR-1 prototype: direct-call testing is sufficient.

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
};

struct HttpResponse {
    int         status = 200;
    std::string body;

    static HttpResponse ok(const std::string& json) { return {200, json}; }
    static HttpResponse not_found() { return {404, R"({"error":"not found"})"}; }
    static HttpResponse bad_request(const std::string& msg) { return {400, "{\"error\":\"" + msg + "\"}"}; }
};

/// HTTP handler for the reference data service.
/// Translates HTTP requests into ref data service calls.
///
/// Auth/RBAC: out of scope for M7 — all requests accepted.
class RefDataHttpHandler {
public:
    using PartitionMap = std::unordered_map<engine::PartitionId, std::unique_ptr<engine::Partition>>;

    explicit RefDataHttpHandler(RefDataService& svc);

    /// Set partition map for live engine state queries (state, BBO, last price).
    void set_partitions(const PartitionMap* partitions) { partitions_ = partitions; }

    HttpResponse handle(const HttpRequest& req);

private:
    const RefDataService& svc_;
    RefDataService& svc_mut_;
    const PartitionMap* partitions_ = nullptr;

    // ─── Read handlers ───────────────────────────────────────
    HttpResponse handle_instruments();
    HttpResponse handle_instrument_detail(const std::string& id_str);
    HttpResponse handle_instrument_by_symbol(const std::string& symbol);
    HttpResponse handle_participants();
    HttpResponse handle_participant_detail(const std::string& id_str);
    HttpResponse handle_sessions(const std::string& id_str);
    HttpResponse handle_participant_risk_limits(const std::string& id_str);

    // ─── Write handlers ──────────────────────────────────────
    HttpResponse handle_create_instrument(const std::string& body);
    HttpResponse handle_update_instrument(const std::string& id_str, const std::string& body);
    HttpResponse handle_create_participant(const std::string& body);

    // ─── JSON helpers ────────────────────────────────────────
    static std::string extract_json_string(const std::string& json, const std::string& key);
    static long long extract_json_int(const std::string& json, const std::string& key, long long default_val = -1);
    static double extract_json_double(const std::string& json, const std::string& key, double default_val = -1.0);
};

}  // namespace refdata
