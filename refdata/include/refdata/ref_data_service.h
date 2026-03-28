#pragma once

#include "engine/types.h"
#include "engine/matching.h"
#include "risk/risk_service.h"
#include "risk/rate_limiter.h"

#include <unordered_map>
#include <string>
#include <optional>
#include <vector>

namespace refdata {

/// In-memory reference data service.
///
/// Provides runtime configuration for instruments, participants,
/// sessions, and risk limits. Serves as the single source of truth
/// for configuration consumed by the engine, gateway, and risk modules.
///
/// Generic by architecture — asset-class-specific parameters are
/// expressed through InstrumentConfig values, not through special
/// service logic.
///
/// FR-1: in-memory only. Phase 2+: backed by persistent store + REST API.
class RefDataService {
public:

    // ─── Enriched instrument info ────────────────────────────

    struct InstrumentInfo {
        engine::InstrumentId instrument_id = 0;
        std::string symbol;
        std::string description;
        std::string currency;        // "USD", "USDT", etc.
        std::string asset_class;     // "digital_asset", "equity", etc.
        engine::InstrumentConfig config{};
    };

    // ─── Instrument management ──────────────────────────────

    void add_instrument(const engine::InstrumentConfig& config);
    void update_instrument(engine::InstrumentId id, const engine::InstrumentConfig& config);
    [[nodiscard]] std::optional<engine::InstrumentConfig> get_instrument(engine::InstrumentId id) const;
    [[nodiscard]] std::vector<engine::InstrumentId> list_instruments() const;

    /// Resolve a symbol string to an InstrumentId. Returns 0 if not found.
    [[nodiscard]] engine::InstrumentId resolve_symbol(const std::string& symbol) const;

    /// Register a symbol → InstrumentId mapping.
    void register_symbol(const std::string& symbol, engine::InstrumentId id);

    // ─── Enriched instrument info ────────────────────────────

    void set_instrument_info(engine::InstrumentId id, const InstrumentInfo& info);
    [[nodiscard]] std::optional<InstrumentInfo> get_instrument_info(engine::InstrumentId id) const;
    [[nodiscard]] std::vector<InstrumentInfo> list_instrument_info() const;
    [[nodiscard]] std::string get_symbol(engine::InstrumentId id) const;

    // ─── Price/quantity conversion ──────────────────────────

    /// Get the price multiplier for an instrument (for decimal → integer conversion).
    [[nodiscard]] engine::Price get_price_multiplier(engine::InstrumentId id) const;

    /// Get the quantity multiplier for an instrument.
    [[nodiscard]] engine::Quantity get_qty_multiplier(engine::InstrumentId id) const;

    // ─── Participant management ─────────────────────────────

    struct ParticipantInfo {
        engine::ParticipantId id;
        std::string           name;
        std::string           sender_comp_id;
        risk::RiskLimits      risk_limits;
    };

    void add_participant(const ParticipantInfo& info);
    [[nodiscard]] std::optional<ParticipantInfo> get_participant(engine::ParticipantId id) const;
    [[nodiscard]] std::vector<engine::ParticipantId> list_participants() const;

    /// Resolve a SenderCompID string to a ParticipantId. Returns 0 if not found.
    [[nodiscard]] engine::ParticipantId resolve_participant(const std::string& sender_comp_id) const;

    // ─── Session configuration ──────────────────────────────

    struct SessionConfig {
        engine::SessionId     session_id;
        engine::ParticipantId participant_id;
        bool                  cod_enabled = false;
        risk::RateLimiter::Config rate_limit;
    };

    void add_session(const SessionConfig& config);
    [[nodiscard]] std::optional<SessionConfig> get_session(engine::SessionId id) const;

    // ─── Partition assignment ───────────────────────────────

    /// Which partition owns this instrument?
    void assign_to_partition(engine::InstrumentId inst_id, engine::PartitionId part_id);
    [[nodiscard]] engine::PartitionId get_partition(engine::InstrumentId inst_id) const;

    // ─── Query counts ───────────────────────────────────────

    [[nodiscard]] uint32_t instrument_count() const { return static_cast<uint32_t>(instruments_.size()); }
    [[nodiscard]] uint32_t participant_count() const { return static_cast<uint32_t>(participants_.size()); }

private:
    std::unordered_map<engine::InstrumentId, engine::InstrumentConfig> instruments_;
    std::unordered_map<std::string, engine::InstrumentId> symbol_map_;
    std::unordered_map<engine::InstrumentId, engine::Price> price_multipliers_;
    std::unordered_map<engine::InstrumentId, engine::Quantity> qty_multipliers_;
    std::unordered_map<engine::ParticipantId, ParticipantInfo> participants_;
    std::unordered_map<std::string, engine::ParticipantId> sender_comp_id_map_;
    std::unordered_map<engine::SessionId, SessionConfig> sessions_;
    std::unordered_map<engine::InstrumentId, engine::PartitionId> partition_map_;
    std::unordered_map<engine::InstrumentId, InstrumentInfo> instrument_info_;
};

}  // namespace refdata
