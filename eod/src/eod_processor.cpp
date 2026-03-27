#include "eod/eod_processor.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace eod {

void EodProcessor::record_trade(const TradeRecord& trade,
                                 engine::ParticipantId maker_id,
                                 engine::ParticipantId taker_id) {
    trades_.push_back(trade);

    // Update instrument summary
    auto& summary = instrument_summaries_[trade.instrument_id];
    summary.instrument_id = trade.instrument_id;
    if (summary.trade_count == 0) {
        summary.open_price = trade.price;
        summary.high_price = trade.price;
        summary.low_price = trade.price;
    } else {
        if (trade.price > summary.high_price) summary.high_price = trade.price;
        if (trade.price < summary.low_price) summary.low_price = trade.price;
    }
    summary.close_price = trade.price;
    summary.trade_count++;
    summary.total_volume += trade.quantity;
    summary.vwap_numerator += trade.price * static_cast<engine::Price>(trade.quantity);

    // Update participant fees
    auto maker_rate_it = fee_rates_.find(maker_id);
    auto taker_rate_it = fee_rates_.find(taker_id);

    auto& maker_fees = participant_fees_[maker_id];
    maker_fees.participant_id = maker_id;
    maker_fees.maker_volume += trade.quantity;
    if (maker_rate_it != fee_rates_.end()) {
        // Fee = notional * rate_bps / 10000
        int64_t notional = trade.price * static_cast<int64_t>(trade.quantity);
        maker_fees.total_fees += notional * maker_rate_it->second.maker_bps / 10000;
    }

    auto& taker_fees = participant_fees_[taker_id];
    taker_fees.participant_id = taker_id;
    taker_fees.taker_volume += trade.quantity;
    if (taker_rate_it != fee_rates_.end()) {
        int64_t notional = trade.price * static_cast<int64_t>(trade.quantity);
        taker_fees.total_fees += notional * taker_rate_it->second.taker_bps / 10000;
    }
}

void EodProcessor::set_fee_rate(engine::ParticipantId participant_id,
                                 int64_t maker_rate_bps, int64_t taker_rate_bps) {
    fee_rates_[participant_id] = {maker_rate_bps, taker_rate_bps};
}

EodReport EodProcessor::generate_report(const std::string& date) const {
    EodReport report;
    report.date = date;

    for (const auto& [id, summary] : instrument_summaries_) {
        report.instrument_summaries.push_back(summary);
        report.total_trades += summary.trade_count;
        report.total_volume += summary.total_volume;
    }

    for (const auto& [id, fees] : participant_fees_) {
        report.participant_fees.push_back(fees);
    }

    // Sort for deterministic output
    std::sort(report.instrument_summaries.begin(), report.instrument_summaries.end(),
              [](const auto& a, const auto& b) { return a.instrument_id < b.instrument_id; });
    std::sort(report.participant_fees.begin(), report.participant_fees.end(),
              [](const auto& a, const auto& b) { return a.participant_id < b.participant_id; });

    return report;
}

void EodProcessor::reset() {
    trades_.clear();
    participant_fees_.clear();
    instrument_summaries_.clear();
    // fee_rates_ preserved across days
}

std::string EodReport::to_text() const {
    std::ostringstream oss;
    oss << "=== END OF DAY REPORT ===" << "\n";
    oss << "Date: " << date << "\n";
    oss << "Total trades: " << total_trades << "\n";
    oss << "Total volume: " << total_volume << "\n\n";

    oss << "--- Instrument Summaries ---" << "\n";
    for (const auto& s : instrument_summaries) {
        oss << "Instrument " << s.instrument_id
            << ": trades=" << s.trade_count
            << " volume=" << s.total_volume
            << " open=" << s.open_price
            << " high=" << s.high_price
            << " low=" << s.low_price
            << " close=" << s.close_price;
        if (s.total_volume > 0) {
            oss << " vwap=" << (s.vwap_numerator / static_cast<int64_t>(s.total_volume));
        }
        oss << "\n";
    }

    oss << "\n--- Participant Fees ---" << "\n";
    for (const auto& f : participant_fees) {
        oss << "Participant " << f.participant_id
            << ": maker_vol=" << f.maker_volume
            << " taker_vol=" << f.taker_volume
            << " total_fees=" << f.total_fees
            << "\n";
    }

    return oss.str();
}

}  // namespace eod
