#include "ops/session_config_store.h"
#include <sstream>
#include <algorithm>

namespace ops {

SessionConfigStore::SessionConfigStore(const std::string& config_path)
    : config_path_(config_path) {}

void SessionConfigStore::add_session(const SessionConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[config.sender_comp_id] = config;
}

bool SessionConfigStore::update_session(const std::string& sender_comp_id, const SessionConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sender_comp_id);
    if (it == sessions_.end()) return false;
    // If the sender_comp_id changed, remove old key and insert new
    if (config.sender_comp_id != sender_comp_id) {
        sessions_.erase(it);
    }
    sessions_[config.sender_comp_id] = config;
    return true;
}

bool SessionConfigStore::remove_session(const std::string& sender_comp_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.erase(sender_comp_id) > 0;
}

std::vector<SessionConfig> SessionConfigStore::list_sessions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SessionConfig> result;
    result.reserve(sessions_.size());
    for (const auto& [key, cfg] : sessions_) {
        result.push_back(cfg);
    }
    return result;
}

std::optional<SessionConfig> SessionConfigStore::get_session(const std::string& sender_comp_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sender_comp_id);
    if (it == sessions_.end()) return std::nullopt;
    return it->second;
}

bool SessionConfigStore::enable_session(const std::string& sender_comp_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sender_comp_id);
    if (it == sessions_.end()) return false;
    it->second.enabled = true;
    return true;
}

bool SessionConfigStore::disable_session(const std::string& sender_comp_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sender_comp_id);
    if (it == sessions_.end()) return false;
    it->second.enabled = false;
    return true;
}

bool SessionConfigStore::reset_sequences(const std::string& sender_comp_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(sender_comp_id);
    if (it == sessions_.end()) return false;
    it->second.inbound_seq = 0;
    it->second.outbound_seq = 0;
    return true;
}

size_t SessionConfigStore::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

// ─── Simple JSON helpers (string concatenation, no library) ─────

static std::string escape_json(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c;
        }
    }
    return out;
}

void SessionConfigStore::save() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream out(config_path_);
    if (!out.is_open()) return;

    out << "[";
    bool first = true;
    for (const auto& [key, cfg] : sessions_) {
        if (!first) out << ",";
        first = false;
        // Only persist config fields (not runtime state)
        out << "{\"sender_comp_id\":\"" << escape_json(cfg.sender_comp_id)
            << "\",\"target_comp_id\":\"" << escape_json(cfg.target_comp_id)
            << "\",\"fix_version\":\"" << escape_json(cfg.fix_version)
            << "\",\"heartbeat_interval\":" << cfg.heartbeat_interval
            << ",\"enabled\":" << (cfg.enabled ? "true" : "false")
            << ",\"participant_id\":" << cfg.participant_id
            << ",\"description\":\"" << escape_json(cfg.description)
            << "\"}";
    }
    out << "]";
}

// ─── Simple JSON parser for load() ──────────────────────────────

static std::string extract_json_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return {};
    auto colon = json.find(':', pos + search.size());
    if (colon == std::string::npos) return {};
    auto q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) return {};
    auto q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    return json.substr(q1 + 1, q2 - q1 - 1);
}

static int64_t extract_json_number(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return 0;
    auto colon = json.find(':', pos + search.size());
    if (colon == std::string::npos) return 0;
    // Skip whitespace
    auto start = colon + 1;
    while (start < json.size() && (json[start] == ' ' || json[start] == '\t')) ++start;
    if (start >= json.size()) return 0;
    try {
        return std::stoll(json.substr(start));
    } catch (...) {
        return 0;
    }
}

static bool extract_json_bool(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return false;
    auto colon = json.find(':', pos + search.size());
    if (colon == std::string::npos) return false;
    auto rest = json.substr(colon + 1);
    // Skip whitespace
    size_t i = 0;
    while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
    return rest.substr(i, 4) == "true";
}

void SessionConfigStore::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream in(config_path_);
    if (!in.is_open()) return;

    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

    // Parse array of objects: find each { ... } block
    sessions_.clear();
    size_t pos = 0;
    while (pos < content.size()) {
        auto obj_start = content.find('{', pos);
        if (obj_start == std::string::npos) break;

        // Find matching closing brace (simple — no nested objects)
        auto obj_end = content.find('}', obj_start);
        if (obj_end == std::string::npos) break;

        std::string obj = content.substr(obj_start, obj_end - obj_start + 1);

        SessionConfig cfg;
        cfg.sender_comp_id = extract_json_string(obj, "sender_comp_id");
        cfg.target_comp_id = extract_json_string(obj, "target_comp_id");
        cfg.fix_version = extract_json_string(obj, "fix_version");
        cfg.heartbeat_interval = static_cast<uint32_t>(extract_json_number(obj, "heartbeat_interval"));
        cfg.enabled = extract_json_bool(obj, "enabled");
        cfg.participant_id = static_cast<engine::ParticipantId>(extract_json_number(obj, "participant_id"));
        cfg.description = extract_json_string(obj, "description");

        if (!cfg.sender_comp_id.empty()) {
            sessions_[cfg.sender_comp_id] = cfg;
        }

        pos = obj_end + 1;
    }
}

}  // namespace ops
