#pragma once
#include <string>
#include <unordered_map>
#include <optional>

namespace ops {

struct UserInfo {
    std::string username;
    std::string role;  // "operator", "admin", "viewer"
};

/// Simple JWT-like auth service for portal.
/// Prototype: uses in-memory user store with API key or username/password.
/// Tokens are simple base64-encoded JSON (not cryptographic JWT for prototype).
///
/// Production: proper JWT with RS256 signing, token expiry, refresh tokens.
class AuthService {
public:
    /// Add a user with username, password, and role.
    void add_user(const std::string& username, const std::string& password, const std::string& role);

    /// Login: returns a token string if credentials valid, nullopt otherwise.
    std::optional<std::string> login(const std::string& username, const std::string& password) const;

    /// Validate a token: returns UserInfo if valid, nullopt if invalid/expired.
    std::optional<UserInfo> validate(const std::string& token) const;

    /// Check if a token has at least the given role level.
    /// Role hierarchy: viewer < operator < admin
    bool has_role(const std::string& token, const std::string& required_role) const;

private:
    struct UserRecord {
        std::string password;
        std::string role;
    };
    std::unordered_map<std::string, UserRecord> users_;  // keyed by username

    /// Simple token encoding: hex(username:role:timestamp)
    std::string encode_token(const std::string& username, const std::string& role) const;
    std::optional<UserInfo> decode_token(const std::string& token) const;

    /// Role hierarchy level (higher = more privileged).
    static int role_level(const std::string& role);
};

}  // namespace ops
