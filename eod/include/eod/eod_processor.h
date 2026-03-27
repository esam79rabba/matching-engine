#pragma once

#include "engine/types.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace eod {

/// A single trade record for EOD aggregation.
struct TradeRecord {
    engine::TradeId       trade_id;
    engine::InstrumentId  instrument_id;
    engine::OrderId       buy_order_id;
    engine::OrderId       sell_order_id;
    engine::ParticipantId buyer_id;
    engine::ParticipantId seller_id;
    engine::Price         price;
    engine::Quantity      quantity;
    engine::Timestamp     timestamp;
};

/// Per-instrument trade summary.
struct InstrumentSummary {
    engine::InstrumentId instrument_id;
    uint32_t             trade_count = 0;
    engine::Quantity     total_volume = 0;
    engine::Price        vwap_numerator = 0;  // sum(price * qty)
    engine::Price        high_price = 0;
    engine::Price        low_price = 0;
    engine::Price        open_price = 0;
    engine::Price        close_price = 0;
};

/// Per-participant fee summary.
struct ParticipantFees {
    engine::ParticipantId participant_id;
    engine::Quantity      maker_volume = 0;
    engine::Quantity      taker_volume = 0;
    int64_t               total_fees = 0;  // In smallest currency unit (may be negative for rebates)
};

/// EOD report output.
struct EodReport {
    std::string                                   date;  // YYYY-MM-DD
    std::vector<InstrumentSummary>                instrument_summaries;
    std::vector<ParticipantFees>                  participant_fees;
    uint32_t                                      total_trades = 0;
    engine::Quantity                              total_volume = 0;

    /// Serialize to a simple text report format.
    [[nodiscard]] std::string to_text() const;
};

/// End-of-day processor.
///
/// Accumulates trade records during the trading day, then produces
/// an EOD report with instrument summaries and fee calculations.
///
/// Generic — no asset-class-specific logic. Fee rates come from
/// configuration (maker/taker rates per participant).
class EodProcessor {
public:

    /// Record a trade. Called from the post-trade event path.
    void record_trade(const TradeRecord& trade,
                      engine::ParticipantId maker_id,
                      engine::ParticipantId taker_id);

    /// Set fee rates for a participant (in basis points or direct rate).
    /// maker_rate: may be negative (rebate). taker_rate: positive.
    void set_fee_rate(engine::ParticipantId participant_id,
                      int64_t maker_rate_bps, int64_t taker_rate_bps);

    /// Generate the EOD report for the current day.
    [[nodiscard]] EodReport generate_report(const std::string& date) const;

    /// Reset for a new trading day.
    void reset();

    /// Query: total trades recorded.
    [[nodiscard]] uint32_t trade_count() const { return static_cast<uint32_t>(trades_.size()); }

private:
    struct FeeRate {
        int64_t maker_bps = 0;  // basis points × 100 (e.g., -10 = -0.1%)
        int64_t taker_bps = 0;
    };

    std::vector<TradeRecord> trades_;
    std::unordered_map<engine::ParticipantId, FeeRate> fee_rates_;
    std::unordered_map<engine::ParticipantId, ParticipantFees> participant_fees_;
    std::unordered_map<engine::InstrumentId, InstrumentSummary> instrument_summaries_;
};

}  // namespace eod
