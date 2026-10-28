#include "ops/trade_audit_trail.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>

#include <openssl/evp.h>

namespace ops {

namespace {

// Extract the raw value text of `"key":` from one JSON-line produced by
// serialize_entry(). Strings are returned without quotes (hashes contain no
// escapes). Returns nullopt when the key is absent or malformed.
std::optional<std::string> json_field(const std::string& line, const std::string& key) {
    const std::string needle = "\"" + key + "\":";
    auto pos = line.find(needle);
    if (pos == std::string::npos) return std::nullopt;
    pos += needle.size();
    if (pos >= line.size()) return std::nullopt;
    if (line[pos] == '"') {
        auto end = line.find('"', pos + 1);
        if (end == std::string::npos) return std::nullopt;
        return line.substr(pos + 1, end - pos - 1);
    }
    auto end = line.find_first_of(",}", pos);
    if (end == std::string::npos || end == pos) return std::nullopt;
    return line.substr(pos, end - pos);
}

template <typename T>
bool parse_unsigned(const std::optional<std::string>& v, T& out) {
    if (!v || v->empty() || (*v)[0] == '-') return false;
    char* end = nullptr;
    unsigned long long x = std::strtoull(v->c_str(), &end, 10);
    if (end == v->c_str() || *end != '\0') return false;
    out = static_cast<T>(x);
    return true;
}

bool parse_signed(const std::optional<std::string>& v, int64_t& out) {
    if (!v || v->empty()) return false;
    char* end = nullptr;
    long long x = std::strtoll(v->c_str(), &end, 10);
    if (end == v->c_str() || *end != '\0') return false;
    out = static_cast<int64_t>(x);
    return true;
}

bool parse_type(const std::string& s, AuditEntryType& out) {
    if (s == "NewOrder")   { out = AuditEntryType::NewOrder;   return true; }
    if (s == "Cancel")     { out = AuditEntryType::Cancel;     return true; }
    if (s == "Fill")       { out = AuditEntryType::Fill;       return true; }
    if (s == "Reject")     { out = AuditEntryType::Reject;     return true; }
    if (s == "Modify")     { out = AuditEntryType::Modify;     return true; }
    if (s == "MassCancel") { out = AuditEntryType::MassCancel; return true; }
    return false;
}

bool parse_entry_line(const std::string& line, AuditEntry& e) {
    uint64_t side = 0, reject = 0;
    auto type = json_field(line, "type");
    auto prev = json_field(line, "prev_hash");
    auto hash = json_field(line, "entry_hash");
    if (!type || !prev || !hash || hash->empty()) return false;
    if (!parse_type(*type, e.type)) return false;
    if (!parse_unsigned(json_field(line, "sequence"), e.sequence)) return false;
    if (!parse_unsigned(json_field(line, "timestamp"), e.timestamp)) return false;
    if (!parse_unsigned(json_field(line, "participant_id"), e.participant_id)) return false;
    if (!parse_unsigned(json_field(line, "instrument_id"), e.instrument_id)) return false;
    if (!parse_unsigned(json_field(line, "order_id"), e.order_id)) return false;
    if (!parse_unsigned(json_field(line, "side"), side)) return false;
    if (!parse_signed(json_field(line, "price"), e.price)) return false;
    if (!parse_unsigned(json_field(line, "quantity"), e.quantity)) return false;
    if (!parse_unsigned(json_field(line, "reject_code"), reject)) return false;
    if (!parse_unsigned(json_field(line, "trade_id"), e.trade_id)) return false;
    if (!parse_unsigned(json_field(line, "contra_order_id"), e.contra_order_id)) return false;
    e.side = static_cast<engine::Side>(side);
    e.reject_code = static_cast<engine::RejectCode>(reject);
    e.prev_hash = *prev;
    e.entry_hash = *hash;
    return true;
}

}  // namespace

TradeAuditTrail::TradeAuditTrail(const std::string& file_path)
    : file_path_(file_path) {
    // Seed chain state from any existing on-disk content BEFORE opening for
    // append, so the first new entry links to the on-disk tail hash.
    const bool needs_newline = load_existing();
    file_.open(file_path, std::ios::app);
    if (needs_newline && file_.is_open()) {
        file_ << "\n";  // isolate a truncated/corrupt tail from the next entry
        file_.flush();
    }
}

// Returns true if the file does not end in a newline (tail must be isolated).
bool TradeAuditTrail::load_existing() {
    std::ifstream in(file_path_, std::ios::in | std::ios::binary);
    if (!in.is_open()) return false;  // no file yet: genesis chain

    std::vector<AuditEntry> loaded;
    std::string line;
    size_t line_no = 0;
    bool corrupt = false;
    bool ends_with_newline = true;

    while (std::getline(in, line)) {
        ++line_no;
        ends_with_newline = !in.eof();
        if (line.empty()) continue;
        AuditEntry e{};
        if (!parse_entry_line(line, e)) {
            corrupt = true;
            break;
        }
        loaded.push_back(std::move(e));
    }

    if (loaded.empty() && !corrupt) return false;  // empty file: keep genesis "0"

    if (corrupt) {
        // Cannot trust the tail: start a fresh chain, but keep sequence
        // numbers monotonic past the last good entry, and shout about it.
        std::cerr << "[AUDIT][CRITICAL] trade audit trail '" << file_path_
                  << "' has an unparseable entry at line " << line_no
                  << "; hash chain CANNOT continue from disk. Starting a NEW chain "
                     "(prev_hash=\"0\") - the on-disk file now has a chain "
                     "discontinuity that requires operator review.\n";
        if (!loaded.empty()) next_sequence_ = loaded.back().sequence + 1;
        last_hash_ = "0";
        return true;
    }

    entries_ = std::move(loaded);
    last_hash_ = entries_.back().entry_hash;
    next_sequence_ = entries_.back().sequence + 1;
    return !ends_with_newline;
}

TradeAuditTrail::~TradeAuditTrail() {
    if (file_.is_open()) file_.close();
}

// ─── Event recording ─────────────────────────────────────────

void TradeAuditTrail::on_new_order(engine::ParticipantId participant,
                                   engine::InstrumentId instrument,
                                   engine::OrderId order_id,
                                   engine::Side side,
                                   engine::Price price,
                                   engine::Quantity quantity,
                                   engine::Timestamp ts) {
    AuditEntry e{};
    e.type           = AuditEntryType::NewOrder;
    e.timestamp      = ts;
    e.participant_id = participant;
    e.instrument_id  = instrument;
    e.order_id       = order_id;
    e.side           = side;
    e.price          = price;
    e.quantity        = quantity;
    e.reject_code    = engine::RejectCode::None;
    e.trade_id       = 0;
    e.contra_order_id = 0;
    append_entry(std::move(e));
}

void TradeAuditTrail::on_cancel(engine::ParticipantId participant,
                                engine::InstrumentId instrument,
                                engine::OrderId order_id,
                                engine::Timestamp ts) {
    AuditEntry e{};
    e.type           = AuditEntryType::Cancel;
    e.timestamp      = ts;
    e.participant_id = participant;
    e.instrument_id  = instrument;
    e.order_id       = order_id;
    e.side           = engine::Side::Buy;  // Neutral default
    e.price          = 0;
    e.quantity        = 0;
    e.reject_code    = engine::RejectCode::None;
    e.trade_id       = 0;
    e.contra_order_id = 0;
    append_entry(std::move(e));
}

void TradeAuditTrail::on_fill(engine::ParticipantId buyer,
                              engine::ParticipantId seller,
                              engine::InstrumentId instrument,
                              engine::OrderId buy_order,
                              engine::OrderId sell_order,
                              engine::TradeId trade_id,
                              engine::Price price,
                              engine::Quantity quantity,
                              engine::Timestamp ts) {
    // Record buyer side
    {
        AuditEntry e{};
        e.type           = AuditEntryType::Fill;
        e.timestamp      = ts;
        e.participant_id = buyer;
        e.instrument_id  = instrument;
        e.order_id       = buy_order;
        e.side           = engine::Side::Buy;
        e.price          = price;
        e.quantity        = quantity;
        e.reject_code    = engine::RejectCode::None;
        e.trade_id       = trade_id;
        e.contra_order_id = sell_order;
        append_entry(std::move(e));
    }
    // Record seller side
    {
        AuditEntry e{};
        e.type           = AuditEntryType::Fill;
        e.timestamp      = ts;
        e.participant_id = seller;
        e.instrument_id  = instrument;
        e.order_id       = sell_order;
        e.side           = engine::Side::Sell;
        e.price          = price;
        e.quantity        = quantity;
        e.reject_code    = engine::RejectCode::None;
        e.trade_id       = trade_id;
        e.contra_order_id = buy_order;
        append_entry(std::move(e));
    }
}

void TradeAuditTrail::on_reject(engine::ParticipantId participant,
                                engine::InstrumentId instrument,
                                engine::OrderId order_id,
                                engine::RejectCode code,
                                engine::Timestamp ts) {
    AuditEntry e{};
    e.type           = AuditEntryType::Reject;
    e.timestamp      = ts;
    e.participant_id = participant;
    e.instrument_id  = instrument;
    e.order_id       = order_id;
    e.side           = engine::Side::Buy;  // Neutral default
    e.price          = 0;
    e.quantity        = 0;
    e.reject_code    = code;
    e.trade_id       = 0;
    e.contra_order_id = 0;
    append_entry(std::move(e));
}

void TradeAuditTrail::on_modify(engine::ParticipantId participant,
                                engine::InstrumentId instrument,
                                engine::OrderId order_id,
                                engine::Price new_price,
                                engine::Quantity new_qty,
                                engine::Timestamp ts) {
    AuditEntry e{};
    e.type           = AuditEntryType::Modify;
    e.timestamp      = ts;
    e.participant_id = participant;
    e.instrument_id  = instrument;
    e.order_id       = order_id;
    e.side           = engine::Side::Buy;  // Neutral default
    e.price          = new_price;
    e.quantity        = new_qty;
    e.reject_code    = engine::RejectCode::None;
    e.trade_id       = 0;
    e.contra_order_id = 0;
    append_entry(std::move(e));
}

// ─── Query methods ───────────────────────────────────────────

std::vector<AuditEntry> TradeAuditTrail::read_recent(size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (count >= entries_.size()) return entries_;
    return std::vector<AuditEntry>(entries_.end() - static_cast<ptrdiff_t>(count),
                                   entries_.end());
}

std::vector<AuditEntry> TradeAuditTrail::read_by_participant(
    engine::ParticipantId pid, size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AuditEntry> result;
    for (const auto& e : entries_) {
        if (e.participant_id == pid) {
            result.push_back(e);
            if (result.size() >= limit) break;
        }
    }
    return result;
}

std::vector<AuditEntry> TradeAuditTrail::read_by_instrument(
    engine::InstrumentId iid, size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AuditEntry> result;
    for (const auto& e : entries_) {
        if (e.instrument_id == iid) {
            result.push_back(e);
            if (result.size() >= limit) break;
        }
    }
    return result;
}

uint64_t TradeAuditTrail::entry_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<uint64_t>(entries_.size());
}

// ─── Chain verification ──────────────────────────────────────

bool TradeAuditTrail::verify_chain() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string expected_prev = "0";  // Genesis

    for (const auto& entry : entries_) {
        // Verify the prev_hash link
        if (entry.prev_hash != expected_prev) return false;

        // Recompute hash and verify
        std::string recomputed = compute_hash(entry);
        if (recomputed != entry.entry_hash) return false;

        expected_prev = entry.entry_hash;
    }
    return true;
}

// ─── Private helpers ─────────────────────────────────────────

std::string TradeAuditTrail::compute_hash(const AuditEntry& entry) const {
    // Build the preimage: all fields that contribute to the hash
    std::ostringstream oss;
    oss << entry.sequence
        << static_cast<int>(entry.type)
        << entry.timestamp
        << entry.participant_id
        << entry.instrument_id
        << entry.order_id
        << static_cast<int>(entry.side)
        << entry.price
        << entry.quantity
        << entry.prev_hash;

    const std::string preimage = oss.str();

    // SHA-256 via OpenSSL EVP API
    unsigned char digest[32];
    unsigned int digest_len = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, preimage.data(), preimage.size());
    EVP_DigestFinal_ex(ctx, digest, &digest_len);
    EVP_MD_CTX_free(ctx);

    std::ostringstream hex;
    hex << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_len; ++i) {
        hex << std::setw(2) << static_cast<int>(digest[i]);
    }
    return hex.str();
}

void TradeAuditTrail::append_entry(AuditEntry entry) {
    std::lock_guard<std::mutex> lock(mutex_);

    entry.sequence  = next_sequence_++;
    entry.prev_hash = last_hash_;
    entry.entry_hash = compute_hash(entry);
    last_hash_ = entry.entry_hash;

    // Persist to file as JSON-line
    if (file_.is_open()) {
        file_ << serialize_entry(entry) << "\n";
        file_.flush();
    }

    entries_.push_back(std::move(entry));
}

std::string TradeAuditTrail::serialize_entry(const AuditEntry& entry) const {
    auto escape = [](const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else out += c;
        }
        return out;
    };

    auto type_str = [](AuditEntryType t) -> const char* {
        switch (t) {
            case AuditEntryType::NewOrder:   return "NewOrder";
            case AuditEntryType::Cancel:     return "Cancel";
            case AuditEntryType::Fill:       return "Fill";
            case AuditEntryType::Reject:     return "Reject";
            case AuditEntryType::Modify:     return "Modify";
            case AuditEntryType::MassCancel: return "MassCancel";
            default:                         return "Unknown";
        }
    };

    std::ostringstream j;
    j << "{"
      << "\"sequence\":" << entry.sequence
      << ",\"type\":\"" << type_str(entry.type) << "\""
      << ",\"timestamp\":" << entry.timestamp
      << ",\"participant_id\":" << entry.participant_id
      << ",\"instrument_id\":" << entry.instrument_id
      << ",\"order_id\":" << entry.order_id
      << ",\"side\":" << static_cast<int>(entry.side)
      << ",\"price\":" << entry.price
      << ",\"quantity\":" << entry.quantity
      << ",\"reject_code\":" << static_cast<int>(entry.reject_code)
      << ",\"trade_id\":" << entry.trade_id
      << ",\"contra_order_id\":" << entry.contra_order_id
      << ",\"prev_hash\":\"" << escape(entry.prev_hash) << "\""
      << ",\"entry_hash\":\"" << escape(entry.entry_hash) << "\""
      << "}";
    return j.str();
}

}  // namespace ops
