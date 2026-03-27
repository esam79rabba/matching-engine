#pragma once

#include "engine/types.h"

#include <cstdint>
#include <unordered_map>

namespace marketdata {

struct InstrumentStats {
    engine::Price open_price = 0;
    engine::Price high_price = 0;
    engine::Price low_price = 0;
    engine::Price close_price = 0;    // Last traded price
    engine::Quantity volume = 0;       // Cumulative volume
    uint64_t trade_count = 0;
    int64_t vwap_numerator = 0;        // Sum of price * qty
    engine::Quantity vwap_denominator = 0;  // Sum of qty

    engine::Price vwap() const {
        return vwap_denominator > 0
            ? static_cast<engine::Price>(vwap_numerator / static_cast<int64_t>(vwap_denominator))
            : 0;
    }
};

/// Tracks real-time OHLCV and VWAP statistics per instrument,
/// plus reference/opening/closing price publication (#53).
class StatisticsTracker {
public:
    /// Record a trade for an instrument.
    void on_trade(engine::InstrumentId instrument_id, engine::Price price, engine::Quantity qty);

    /// Get current statistics for an instrument.
    [[nodiscard]] InstrumentStats get_stats(engine::InstrumentId instrument_id) const;

    /// Reset statistics for a single instrument (session reset).
    void reset(engine::InstrumentId instrument_id);

    /// Reset statistics for all instruments.
    void reset_all();

    // ─── Reference / Opening / Closing prices (#53) ──────────

    void set_reference_price(engine::InstrumentId instrument_id, engine::Price price);
    void set_opening_price(engine::InstrumentId instrument_id, engine::Price price);
    void set_closing_price(engine::InstrumentId instrument_id, engine::Price price);

    [[nodiscard]] engine::Price reference_price(engine::InstrumentId instrument_id) const;
    [[nodiscard]] engine::Price opening_price(engine::InstrumentId instrument_id) const;
    [[nodiscard]] engine::Price closing_price(engine::InstrumentId instrument_id) const;

private:
    std::unordered_map<engine::InstrumentId, InstrumentStats> stats_;

    struct SessionPrices {
        engine::Price reference = 0;
        engine::Price opening = 0;
        engine::Price closing = 0;
    };
    std::unordered_map<engine::InstrumentId, SessionPrices> session_prices_;
};

}  // namespace marketdata
