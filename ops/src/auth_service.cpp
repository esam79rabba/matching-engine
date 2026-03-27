#include "ops/auth_service.h"

#include <chrono>
#include <sstream>
#include <iomanip>

#ifdef OPENSSL_AVAILABLE
#include <openssl/hmac.h>
#include <openssl/evp.h>
#endif

namespace ops {

namespace {

/// Encode a string to hex.
std::string to_hex(const std::string& input) {
    std::ostringstream oss;
    for (unsigned char c : input) {
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(c);
    }
    return oss.str();
}

/// Encode raw bytes to hex.
std::string to_hex(const unsigned char* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

/// Decode a hex string back to original.
std::string from_hex(const std::string& hex) {
    std::string result;
    if (hex.size() % 2 != 0) return {};
    for (size_t i = 0; i < hex.size(); i += 2) {
        auto byte_str = hex.substr(i, 2);
        char* end = nullptr;
        auto byte = static_cast<char>(std::strtol(byte_str.c_str(), &end, 16));
        if (end != byte_str.c_str() + 2) return {};
        result.push_back(byte);
    }
    return result;
}

#ifdef OPENSSL_AVAILABLE
/// Compute HMAC-SHA256 of data with key, return raw bytes as string.
std::string hmac_sha256(const std::string& key, const std::string& data) {
    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int result_len = 0;

    HMAC(EVP_sha256(),
         key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         result, &result_len);

    return to_hex(result, result_len);
}
#endif

/// Get current Unix timestamp in seconds.
int64_t current_timestamp_seconds() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

}  // namespace

void AuthService::set_signing_key(const std::string& key) {
    signing_key_ = key;
}

void AuthService::add_user(const std::string& username, const std::string& password, const std::string& role) {
    users_[username] = UserRecord{password, role};
}

std::optional<std::string> AuthService::login(const std::string& username, const std::string& password) const {
    auto it = users_.find(username);
    if (it == users_.end()) return std::nullopt;
    if (it->second.password != password) return std::nullopt;
    return encode_token(username, it->second.role);
}

std::optional<UserInfo> AuthService::validate(const std::string& token) const {
    auto info = decode_token(token);
    if (!info) return std::nullopt;
    // Verify user still exists
    if (users_.find(info->username) == users_.end()) return std::nullopt;
    return info;
}

bool AuthService::has_role(const std::string& token, const std::string& required_role) const {
    auto info = validate(token);
    if (!info) return false;
    return role_level(info->role) >= role_level(required_role);
}

std::string AuthService::encode_token(const std::string& username, const std::string& role) const {
    auto ts = current_timestamp_seconds();
    std::string payload = username + ":" + role + ":" + std::to_string(ts);
    std::string payload_hex = to_hex(payload);

#ifdef OPENSSL_AVAILABLE
    std::string signature = hmac_sha256(signing_key_, payload);
    return payload_hex + "." + signature;
#else
    // Fallback: hex-only encoding (no signing — insecure, for builds without OpenSSL)
    return payload_hex;
#endif
}

std::optional<UserInfo> AuthService::decode_token(const std::string& token) const {
    std::string payload_hex;
    std::string token_signature;

#ifdef OPENSSL_AVAILABLE
    // Token format: hex(payload).hex(signature)
    auto dot = token.find('.');
    if (dot == std::string::npos) return std::nullopt;

    payload_hex = token.substr(0, dot);
    token_signature = token.substr(dot + 1);
#else
    // Fallback: hex-only encoding
    payload_hex = token;
#endif

    std::string payload = from_hex(payload_hex);
    if (payload.empty()) return std::nullopt;

    // Parse "username:role:timestamp"
    auto first_colon = payload.find(':');
    if (first_colon == std::string::npos) return std::nullopt;

    auto second_colon = payload.find(':', first_colon + 1);
    if (second_colon == std::string::npos) return std::nullopt;

    std::string username = payload.substr(0, first_colon);
    std::string role = payload.substr(first_colon + 1, second_colon - first_colon - 1);
    std::string ts_str = payload.substr(second_colon + 1);

    if (username.empty() || role.empty() || ts_str.empty()) return std::nullopt;

#ifdef OPENSSL_AVAILABLE
    // Verify HMAC signature
    std::string expected_signature = hmac_sha256(signing_key_, payload);
    if (token_signature != expected_signature) return std::nullopt;
#endif

    // Check token expiry
    try {
        int64_t token_ts = std::stoll(ts_str);
        int64_t now = current_timestamp_seconds();
        if (now - token_ts > TOKEN_EXPIRY_SECONDS) return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }

    return UserInfo{username, role};
}

int AuthService::role_level(const std::string& role) {
    if (role == "admin") return 3;
    if (role == "operator") return 2;
    if (role == "viewer") return 1;
    return 0;
}

}  // namespace ops
