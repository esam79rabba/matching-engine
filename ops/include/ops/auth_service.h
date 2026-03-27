#pragma once
#include <string>
#include <unordered_map>
#include <optional>
#include <cstdint>

namespace ops {

struct UserInfo {
    std::string username;
    std::string role;  // "operator", "admin", "viewer"
};

/// JWT-like auth service for portal with HMAC-SHA256 signed tokens.
///
/// Token format: hex(payload).hex(hmac_signature)
/// Payload: username:role:timestamp
/// Tokens expire after 24 hours.
///
/// Uses OpenSSL HMAC when available (OPENSSL_AVAILABLE defined).
/// Falls back to hex-only encoding without HMAC if OpenSSL is absent.
class AuthService {
public:
    /// Set the HMAC signing key. Should be loaded from environment in production.
    void set_signing_key(const std::string& key);

    /// Add a user with username, password, and role.
    void add_user(const std::string& username, const std::string& password, const std::string& role);

    /// Login: returns a token string if credentials valid, nullopt otherwise.
    std::optional<std::string> login(const std::string& username, const std::string& password) const;

    /// Validate a token: returns UserInfo if valid, nullopt if invalid/expired.
    std::optional<UserInfo> validate(const std::string& token) const;

    /// Check if a token has at least the given role level.
    /// Role hierarchy: viewer < operator < admin
    bool has_role(const std::string& token, const std::string& required_role) const;

    /// Token expiry duration in seconds (default: 24 hours).
    static constexpr int64_t TOKEN_EXPIRY_SECONDS = 86400;

private:
    struct UserRecord {
        std::string password;
        std::string role;
    };
    std::unordered_map<std::string, UserRecord> users_;  // keyed by username
    std::string signing_key_ = "default-signing-key-change-in-production";

    /// HMAC-signed token encoding: hex(payload).hex(hmac_signature)
    std::string encode_token(const std::string& username, const std::string& role) const;
    std::optional<UserInfo> decode_token(const std::string& token) const;

    /// Hash a password using SHA-256 (when OpenSSL available) for storage.
    static std::string hash_password(const std::string& password);

    /// Role hierarchy level (higher = more privileged).
    static int role_level(const std::string& role);
};

}  // namespace ops
