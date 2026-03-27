#pragma once

#include "ops/ops_controller.h"
#include "ops/auth_middleware.h"
#include "ops/auth_service.h"
#include "refdata/ref_data_http.h"

#include <string>
#include <atomic>
#include <functional>
#include <cstdint>

namespace ops {

/// Minimal single-threaded HTTP server for ops and ref-data endpoints.
///
/// Prototype transport — sufficient for local development and integration testing.
/// Production: replace with a proper HTTP framework (Crow, httplib, Boost.Beast).
///
/// Supports GET and POST methods only. Parses basic HTTP/1.1 requests.
/// No TLS, no chunked encoding, no keep-alive (prototype).
///
/// Deferred to Phase 2+:
///   - TLS/mTLS
///   - HTTP/2
///   - Connection keep-alive
///   - Multi-threaded request handling
///   - Auth/RBAC middleware
class HttpServer {
public:
    /// Handler function: takes method + path + body + raw_request, returns status + body.
    using Handler = std::function<std::pair<int, std::string>(
        const std::string& method, const std::string& path, const std::string& body,
        const std::string& raw_request)>;

    explicit HttpServer(uint16_t port);
    ~HttpServer();

    /// Set the unified request handler.
    void set_handler(Handler handler) { handler_ = std::move(handler); }

    /// Wire ops controller and ref-data handler as the combined handler.
    /// If auth_service is non-null, refdata write endpoints require admin role.
    void wire(OpsController& ops, refdata::RefDataHttpHandler& refdata,
              AuthService* auth_service = nullptr);

    /// Start serving (blocking — call from a dedicated thread).
    bool start();

    /// Stop the server.
    void stop();

    /// Set the authentication middleware.
    void set_auth(const AuthMiddleware& auth);

    /// Set allowed CORS origin (e.g., "*" or "http://localhost:3000").
    void set_cors_origin(const std::string& origin) { cors_origin_ = origin; }

    [[nodiscard]] bool is_running() const { return running_.load(); }
    [[nodiscard]] uint16_t port() const { return port_; }

private:
    uint16_t port_;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    Handler handler_;
    AuthMiddleware auth_;
    std::string cors_origin_;

    void handle_request(int client_fd);

    /// Parse a raw HTTP request into method, path, body.
    static bool parse_http_request(const std::string& raw,
                                    std::string& method, std::string& path,
                                    std::string& body);

    /// Build a minimal HTTP response with CORS headers.
    std::string build_http_response(int status, const std::string& body) const;
};

}  // namespace ops
