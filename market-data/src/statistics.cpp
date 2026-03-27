#include "marketdata/statistics.h"

namespace marketdata {

void StatisticsTracker::on_trade(engine::InstrumentId instrument_id,
                                  engine::Price price, engine::Quantity qty) {
    auto& s = stats_[instrument_id];

    // First trade sets the open price.
    if (s.trade_count == 0) {
        s.open_price = price;
        s.high_price = price;
        s.low_price = price;
    } else {
        if (price > s.high_price) s.high_price = price;
        if (price < s.low_price) s.low_price = price;
    }

    s.close_price = price;
    s.volume += qty;
    s.trade_count += 1;
    s.vwap_numerator += static_cast<int64_t>(price) * static_cast<int64_t>(qty);
    s.vwap_denominator += qty;
}

InstrumentStats StatisticsTracker::get_stats(engine::InstrumentId instrument_id) const {
    auto it = stats_.find(instrument_id);
    if (it == stats_.end()) {
        return {};
    }
    return it->second;
}

void StatisticsTracker::reset(engine::InstrumentId instrument_id) {
    stats_.erase(instrument_id);
    session_prices_.erase(instrument_id);
}

void StatisticsTracker::reset_all() {
    stats_.clear();
    session_prices_.clear();
}

// ─── Reference / Opening / Closing prices ────────────────────

void StatisticsTracker::set_reference_price(engine::InstrumentId instrument_id, engine::Price price) {
    session_prices_[instrument_id].reference = price;
}

void StatisticsTracker::set_opening_price(engine::InstrumentId instrument_id, engine::Price price) {
    session_prices_[instrument_id].opening = price;
}

void StatisticsTracker::set_closing_price(engine::InstrumentId instrument_id, engine::Price price) {
    session_prices_[instrument_id].closing = price;
}

engine::Price StatisticsTracker::reference_price(engine::InstrumentId instrument_id) const {
    auto it = session_prices_.find(instrument_id);
    return (it != session_prices_.end()) ? it->second.reference : 0;
}

engine::Price StatisticsTracker::opening_price(engine::InstrumentId instrument_id) const {
    auto it = session_prices_.find(instrument_id);
    return (it != session_prices_.end()) ? it->second.opening : 0;
}

engine::Price StatisticsTracker::closing_price(engine::InstrumentId instrument_id) const {
    auto it = session_prices_.find(instrument_id);
    return (it != session_prices_.end()) ? it->second.closing : 0;
}

}  // namespace marketdata
