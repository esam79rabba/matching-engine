#pragma once
#include <string>
#include <functional>
#include <unordered_set>
#include <unordered_map>

namespace ops {

/// Simple API-key-based authentication middleware for ops endpoints.
/// Prototype auth — not full RBAC. Sufficient for pilot access control.
///
/// Usage:
///   AuthMiddleware auth;
///   auth.add_api_key("my-secret-key-123");
///   auth.set_open_paths({"/health", "/api/v1/health"});
///
///   // In request handler:
///   if (!auth.check(request_headers, path)) {
///       return 401 Unauthorized;
///   }
class AuthMiddleware {
public:
    /// Add a valid API key.
    void add_api_key(const std::string& key);

    /// Set paths that don't require authentication.
    void set_open_paths(const std::unordered_set<std::string>& paths);

    /// Check if a request is authorized.
    /// Looks for "Authorization: Bearer <key>" or "X-API-Key: <key>" header.
    /// Returns true if path is open or key is valid.
    bool check(const std::unordered_map<std::string, std::string>& headers,
               const std::string& path) const;

    /// Check using a raw HTTP request string (extracts headers internally).
    bool check_raw(const std::string& raw_http, const std::string& path) const;

    /// Extract headers from raw HTTP request.
    static std::unordered_map<std::string, std::string> parse_headers(const std::string& raw_http);

    /// Set a token validator function (e.g., from AuthService).
    /// If set, Bearer tokens are validated through this in addition to API key check.
    using TokenValidator = std::function<bool(const std::string&)>;
    void set_token_validator(TokenValidator fn) { token_validator_ = std::move(fn); }

    /// Is auth enabled (any keys configured)?
    bool is_enabled() const { return !api_keys_.empty(); }

private:
    std::unordered_set<std::string> api_keys_;
    std::unordered_set<std::string> open_paths_;
    TokenValidator token_validator_;
};

}  // namespace ops
