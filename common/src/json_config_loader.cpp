#include "config/json_config_loader.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace config {

// ─── Minimal JSON tokenizer ─────────────────────────────────────────
// Handles the specific JSON structures in instruments.json and sessions.json.
// No external library dependencies.

namespace {

/// Skip whitespace characters in the input string starting at pos.
void skip_ws(const std::string& s, size_t& pos) {
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) {
        ++pos;
    }
}

/// Read a JSON string value (assumes pos is at the opening quote).
/// Returns the string content without quotes. Advances pos past the closing quote.
std::string read_string(const std::string& s, size_t& pos) {
    if (pos >= s.size() || s[pos] != '"') return {};
    ++pos;  // skip opening quote
    std::string result;
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\' && pos + 1 < s.size()) {
            ++pos;
            switch (s[pos]) {
                case '"':  result += '"'; break;
                case '\\': result += '\\'; break;
                case '/':  result += '/'; break;
                case 'n':  result += '\n'; break;
                case 't':  result += '\t'; break;
                case 'r':  result += '\r'; break;
                default:   result += s[pos]; break;
            }
        } else {
            result += s[pos];
        }
        ++pos;
    }
    if (pos < s.size()) ++pos;  // skip closing quote
    return result;
}

/// Read a JSON numeric value (integer or floating point) as a string.
/// Advances pos past the number.
std::string read_number(const std::string& s, size_t& pos) {
    size_t start = pos;
    if (pos < s.size() && s[pos] == '-') ++pos;
    while (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) ||
                               s[pos] == '.' || s[pos] == 'e' || s[pos] == 'E' ||
                               s[pos] == '+' || s[pos] == '-')) {
        // Avoid consuming a '-' that isn't part of scientific notation
        if ((s[pos] == '-' || s[pos] == '+') && pos > start &&
            s[pos - 1] != 'e' && s[pos - 1] != 'E') {
            break;
        }
        ++pos;
    }
    return s.substr(start, pos - start);
}

/// A minimal JSON value representation.
struct JsonValue {
    enum Type { Null, String, Number, Bool, Array, Object };
    Type type = Null;
    std::string str_val;
    double num_val = 0.0;
    bool bool_val = false;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj;

    /// Convenience accessors
    const JsonValue* find(const std::string& key) const {
        if (type != Object) return nullptr;
        for (const auto& [k, v] : obj) {
            if (k == key) return &v;
        }
        return nullptr;
    }

    std::string get_string(const std::string& key, const std::string& def = {}) const {
        auto* v = find(key);
        if (v && v->type == String) return v->str_val;
        return def;
    }

    int64_t get_int(const std::string& key, int64_t def = 0) const {
        auto* v = find(key);
        if (v && v->type == Number) return static_cast<int64_t>(v->num_val);
        return def;
    }

    uint64_t get_uint(const std::string& key, uint64_t def = 0) const {
        auto* v = find(key);
        if (v && v->type == Number) return static_cast<uint64_t>(v->num_val);
        return def;
    }

    double get_double(const std::string& key, double def = 0.0) const {
        auto* v = find(key);
        if (v && v->type == Number) return v->num_val;
        return def;
    }

    bool get_bool(const std::string& key, bool def = false) const {
        auto* v = find(key);
        if (v && v->type == Bool) return v->bool_val;
        return def;
    }
};

/// Forward declaration for recursive parsing.
JsonValue parse_value(const std::string& s, size_t& pos);

/// Parse a JSON object { "key": value, ... }
JsonValue parse_object(const std::string& s, size_t& pos) {
    JsonValue result;
    result.type = JsonValue::Object;
    if (pos >= s.size() || s[pos] != '{') return result;
    ++pos;  // skip '{'

    skip_ws(s, pos);
    if (pos < s.size() && s[pos] == '}') { ++pos; return result; }

    while (pos < s.size()) {
        skip_ws(s, pos);
        if (pos >= s.size() || s[pos] != '"') break;
        std::string key = read_string(s, pos);

        skip_ws(s, pos);
        if (pos >= s.size() || s[pos] != ':') break;
        ++pos;  // skip ':'

        skip_ws(s, pos);
        auto val = parse_value(s, pos);
        result.obj.emplace_back(std::move(key), std::move(val));

        skip_ws(s, pos);
        if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
        if (pos < s.size() && s[pos] == '}') { ++pos; break; }
        break;
    }
    return result;
}

/// Parse a JSON array [ value, ... ]
JsonValue parse_array(const std::string& s, size_t& pos) {
    JsonValue result;
    result.type = JsonValue::Array;
    if (pos >= s.size() || s[pos] != '[') return result;
    ++pos;  // skip '['

    skip_ws(s, pos);
    if (pos < s.size() && s[pos] == ']') { ++pos; return result; }

    while (pos < s.size()) {
        skip_ws(s, pos);
        auto val = parse_value(s, pos);
        result.arr.push_back(std::move(val));

        skip_ws(s, pos);
        if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
        if (pos < s.size() && s[pos] == ']') { ++pos; break; }
        break;
    }
    return result;
}

/// Parse any JSON value.
JsonValue parse_value(const std::string& s, size_t& pos) {
    skip_ws(s, pos);
    if (pos >= s.size()) return {};

    if (s[pos] == '"') {
        JsonValue v;
        v.type = JsonValue::String;
        v.str_val = read_string(s, pos);
        return v;
    }
    if (s[pos] == '{') {
        return parse_object(s, pos);
    }
    if (s[pos] == '[') {
        return parse_array(s, pos);
    }
    if (s[pos] == 't' && s.compare(pos, 4, "true") == 0) {
        pos += 4;
        JsonValue v;
        v.type = JsonValue::Bool;
        v.bool_val = true;
        return v;
    }
    if (s[pos] == 'f' && s.compare(pos, 5, "false") == 0) {
        pos += 5;
        JsonValue v;
        v.type = JsonValue::Bool;
        v.bool_val = false;
        return v;
    }
    if (s[pos] == 'n' && s.compare(pos, 4, "null") == 0) {
        pos += 4;
        return {};
    }
    // Number
    if (s[pos] == '-' || std::isdigit(static_cast<unsigned char>(s[pos]))) {
        JsonValue v;
        v.type = JsonValue::Number;
        std::string num_str = read_number(s, pos);
        v.num_val = std::stod(num_str);
        return v;
    }
    return {};
}

/// Parse a complete JSON document from a string.
JsonValue parse_json(const std::string& s) {
    size_t pos = 0;
    return parse_value(s, pos);
}

/// Read an entire file into a string.
std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// Map residual policy string to enum.
engine::ResidualPolicy parse_residual_policy(const std::string& s) {
    if (s == "market_to_limit_collar") return engine::ResidualPolicy::MarketToLimitCollar;
    return engine::ResidualPolicy::Cancel;  // default
}

/// Map STP mode string to enum.
engine::STPMode parse_stp_mode(const std::string& s) {
    // Only CancelNewest is implemented in FR-1
    (void)s;
    return engine::STPMode::CancelNewest;
}

}  // anonymous namespace

// ─── Public API ─────────────────────────────────────────────────────

LoadResult JsonConfigLoader::load_instruments(const std::string& path) {
    LoadResult result;

    std::string content = read_file(path);
    if (content.empty()) {
        result.error = "Cannot open or empty file: " + path;
        return result;
    }

    auto root = parse_json(content);
    if (root.type != JsonValue::Object) {
        result.error = "Invalid JSON: expected root object in " + path;
        return result;
    }

    auto* instruments_val = root.find("instruments");
    if (!instruments_val || instruments_val->type != JsonValue::Array) {
        result.error = "Missing or invalid 'instruments' array in " + path;
        return result;
    }

    for (const auto& item : instruments_val->arr) {
        if (item.type != JsonValue::Object) continue;

        InstrumentEntry entry{};
        entry.instrument_id = static_cast<engine::InstrumentId>(item.get_uint("instrument_id"));
        entry.symbol = item.get_string("symbol");
        entry.description = item.get_string("description");
        entry.partition_id = static_cast<engine::PartitionId>(item.get_uint("partition_id", 1));

        auto& cfg = entry.config;
        cfg.instrument_id = entry.instrument_id;
        cfg.tick_size = static_cast<engine::Price>(item.get_int("tick_size", 1));
        cfg.lot_size = static_cast<engine::Quantity>(item.get_uint("lot_size", 1));
        cfg.min_quantity = static_cast<engine::Quantity>(item.get_uint("min_quantity", 1));
        cfg.max_quantity = static_cast<engine::Quantity>(item.get_uint("max_quantity", 1000000));
        cfg.static_ref_price = static_cast<engine::Price>(item.get_int("static_ref_price", 0));
        cfg.static_band_pct = item.get_double("static_band_pct", 10.0);
        cfg.dynamic_band_pct = item.get_double("dynamic_band_pct", 10.0);
        cfg.residual_policy = parse_residual_policy(item.get_string("residual_policy", "cancel"));
        cfg.stp_mode = parse_stp_mode(item.get_string("stp_mode", "cancel_newest"));
        cfg.collar_ticks = static_cast<uint32_t>(item.get_uint("collar_ticks", 10));
        cfg.qty_increase_loses_priority = item.get_bool("qty_increase_loses_priority", true);

        result.instruments.push_back(std::move(entry));
    }

    result.success = true;
    return result;
}

LoadResult JsonConfigLoader::load_sessions(const std::string& path) {
    LoadResult result;

    std::string content = read_file(path);
    if (content.empty()) {
        result.error = "Cannot open or empty file: " + path;
        return result;
    }

    auto root = parse_json(content);
    if (root.type != JsonValue::Object) {
        result.error = "Invalid JSON: expected root object in " + path;
        return result;
    }

    SessionConfig session_cfg;

    // Parse session_profile
    auto* profile = root.find("session_profile");
    if (profile && profile->type == JsonValue::Object) {
        session_cfg.name = profile->get_string("name");
        session_cfg.timezone = profile->get_string("timezone");

        auto* transitions = profile->find("transitions");
        if (transitions && transitions->type == JsonValue::Array) {
            for (const auto& t : transitions->arr) {
                if (t.type != JsonValue::Object) continue;
                SessionScheduleEntry entry;
                entry.state = t.get_string("state");
                entry.time = t.get_string("time");
                session_cfg.transitions.push_back(std::move(entry));
            }
        }
    }

    // Parse participants
    auto* participants = root.find("participants");
    if (participants && participants->type == JsonValue::Array) {
        for (const auto& p : participants->arr) {
            if (p.type != JsonValue::Object) continue;
            ParticipantConfig pc{};
            pc.participant_id = static_cast<engine::ParticipantId>(p.get_uint("participant_id"));
            pc.name = p.get_string("name");
            pc.sender_comp_id = p.get_string("sender_comp_id");
            pc.max_order_rate = static_cast<uint32_t>(p.get_uint("max_order_rate"));
            pc.fat_finger_price_pct = p.get_double("fat_finger_price_pct");
            pc.fat_finger_max_notional = p.get_double("fat_finger_max_notional");
            session_cfg.participants.push_back(std::move(pc));
        }
    }

    result.session = std::move(session_cfg);
    result.success = true;
    return result;
}

}  // namespace config
