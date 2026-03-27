#include "ops/auth_middleware.h"

#include <sstream>

namespace ops {

void AuthMiddleware::add_api_key(const std::string& key) {
    api_keys_.insert(key);
}

void AuthMiddleware::set_open_paths(const std::unordered_set<std::string>& paths) {
    open_paths_ = paths;
}

bool AuthMiddleware::check(const std::unordered_map<std::string, std::string>& headers,
                           const std::string& path) const {
    // Open paths always allowed
    if (open_paths_.count(path) > 0) {
        return true;
    }

    // If no keys configured, auth is disabled — allow all
    if (api_keys_.empty()) {
        return true;
    }

    // Check "Authorization: Bearer <key>" header
    auto auth_it = headers.find("Authorization");
    if (auth_it != headers.end()) {
        const std::string& value = auth_it->second;
        const std::string bearer_prefix = "Bearer ";
        if (value.size() > bearer_prefix.size() &&
            value.substr(0, bearer_prefix.size()) == bearer_prefix) {
            std::string token = value.substr(bearer_prefix.size());
            // Check as API key first
            if (api_keys_.count(token) > 0) {
                return true;
            }
            // Check as JWT token via validator
            if (token_validator_ && token_validator_(token)) {
                return true;
            }
        }
    }

    // Check "X-API-Key: <key>" header
    auto key_it = headers.find("X-API-Key");
    if (key_it != headers.end()) {
        if (api_keys_.count(key_it->second) > 0) {
            return true;
        }
    }

    return false;
}

bool AuthMiddleware::check_raw(const std::string& raw_http, const std::string& path) const {
    auto headers = parse_headers(raw_http);
    return check(headers, path);
}

std::unordered_map<std::string, std::string> AuthMiddleware::parse_headers(const std::string& raw_http) {
    std::unordered_map<std::string, std::string> headers;

    // Skip the first line (request line: "GET /path HTTP/1.1")
    auto first_line_end = raw_http.find("\r\n");
    if (first_line_end == std::string::npos) return headers;

    std::string header_section = raw_http.substr(first_line_end + 2);

    // Headers end at "\r\n\r\n"
    auto headers_end = header_section.find("\r\n\r\n");
    if (headers_end != std::string::npos) {
        header_section = header_section.substr(0, headers_end);
    }

    // Parse each "Header-Name: value" line
    std::istringstream stream(header_section);
    std::string line;
    while (std::getline(stream, line)) {
        // Remove trailing \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) break;

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);

        // Trim leading whitespace from value
        auto val_start = value.find_first_not_of(' ');
        if (val_start != std::string::npos) {
            value = value.substr(val_start);
        }

        headers[name] = value;
    }

    return headers;
}

}  // namespace ops
