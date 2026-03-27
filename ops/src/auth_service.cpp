#include "ops/auth_service.h"

#include <chrono>
#include <sstream>
#include <iomanip>

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

}  // namespace

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
    auto now = std::chrono::system_clock::now();
    auto ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::string payload = username + ":" + role + ":" + std::to_string(ts);
    return to_hex(payload);
}

std::optional<UserInfo> AuthService::decode_token(const std::string& token) const {
    std::string decoded = from_hex(token);
    if (decoded.empty()) return std::nullopt;

    // Parse "username:role:timestamp"
    auto first_colon = decoded.find(':');
    if (first_colon == std::string::npos) return std::nullopt;

    auto second_colon = decoded.find(':', first_colon + 1);
    if (second_colon == std::string::npos) return std::nullopt;

    std::string username = decoded.substr(0, first_colon);
    std::string role = decoded.substr(first_colon + 1, second_colon - first_colon - 1);

    if (username.empty() || role.empty()) return std::nullopt;

    return UserInfo{username, role};
}

int AuthService::role_level(const std::string& role) {
    if (role == "admin") return 3;
    if (role == "operator") return 2;
    if (role == "viewer") return 1;
    return 0;
}

}  // namespace ops
