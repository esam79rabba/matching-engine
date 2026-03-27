#include "ops/trade_audit_trail.h"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <sstream>

#ifdef OPENSSL_AVAILABLE
#include <openssl/evp.h>
#endif

namespace ops {

TradeAuditTrail::TradeAuditTrail(const std::string& file_path)
    : file_path_(file_path) {
    file_.open(file_path, std::ios::app);
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

#ifdef OPENSSL_AVAILABLE
    // Production: real SHA-256 via OpenSSL EVP API
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
#else
    // Fallback: std::hash placeholder.
    // NOTE: Production deployments MUST use SHA-256 (OpenSSL).
    // This placeholder is deterministic and sufficient for chain integrity
    // testing but does NOT provide cryptographic security.
    auto h = std::hash<std::string>{}(preimage);
    std::ostringstream hex;
    hex << std::hex << std::setfill('0') << std::setw(16) << h;
    return hex.str();
#endif
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
