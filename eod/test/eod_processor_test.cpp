#include "eod/eod_processor.h"
#include <gtest/gtest.h>

namespace eod {
namespace {

TradeRecord make_trade(engine::TradeId id, engine::InstrumentId inst,
                        engine::Price price, engine::Quantity qty,
                        engine::ParticipantId buyer = 1, engine::ParticipantId seller = 2) {
    return {id, inst, 100 + id, 200 + id, buyer, seller, price, qty, 1000 * id};
}

TEST(EodProcessorTest, EmptyDayReport) {
    EodProcessor eod;
    auto report = eod.generate_report("2026-03-25");
    EXPECT_EQ(report.total_trades, 0u);
    EXPECT_EQ(report.total_volume, 0u);
    EXPECT_TRUE(report.instrument_summaries.empty());
}

TEST(EodProcessorTest, SingleTradeAggregation) {
    EodProcessor eod;
    eod.record_trade(make_trade(1, 100, 50000, 10), 2, 1);  // seller=maker, buyer=taker

    auto report = eod.generate_report("2026-03-25");
    EXPECT_EQ(report.total_trades, 1u);
    EXPECT_EQ(report.total_volume, 10u);

    ASSERT_EQ(report.instrument_summaries.size(), 1u);
    EXPECT_EQ(report.instrument_summaries[0].instrument_id, 100u);
    EXPECT_EQ(report.instrument_summaries[0].open_price, 50000);
    EXPECT_EQ(report.instrument_summaries[0].close_price, 50000);
    EXPECT_EQ(report.instrument_summaries[0].high_price, 50000);
    EXPECT_EQ(report.instrument_summaries[0].low_price, 50000);
}

TEST(EodProcessorTest, MultipleTradesOHLC) {
    EodProcessor eod;
    eod.record_trade(make_trade(1, 100, 50000, 10), 2, 1);  // Open
    eod.record_trade(make_trade(2, 100, 52000, 5), 2, 1);   // High
    eod.record_trade(make_trade(3, 100, 48000, 8), 2, 1);   // Low
    eod.record_trade(make_trade(4, 100, 51000, 3), 2, 1);   // Close

    auto report = eod.generate_report("2026-03-25");
    EXPECT_EQ(report.total_trades, 4u);
    EXPECT_EQ(report.total_volume, 26u);

    ASSERT_EQ(report.instrument_summaries.size(), 1u);
    const auto& s = report.instrument_summaries[0];
    EXPECT_EQ(s.open_price, 50000);
    EXPECT_EQ(s.high_price, 52000);
    EXPECT_EQ(s.low_price, 48000);
    EXPECT_EQ(s.close_price, 51000);
}

TEST(EodProcessorTest, FeeCalculation) {
    EodProcessor eod;
    // Participant 1: maker rate = -10 bps (rebate), taker rate = 20 bps
    eod.set_fee_rate(1, -10, 20);
    // Participant 2: maker rate = -5 bps, taker rate = 15 bps
    eod.set_fee_rate(2, -5, 15);

    // Trade: price=50000, qty=10 → notional=500000
    // Maker = participant 2 (fee: 500000 * -5 / 10000 = -250)
    // Taker = participant 1 (fee: 500000 * 20 / 10000 = 1000)
    eod.record_trade(make_trade(1, 100, 50000, 10), 2, 1);

    auto report = eod.generate_report("2026-03-25");
    ASSERT_EQ(report.participant_fees.size(), 2u);

    // Find participant 2 (maker)
    const ParticipantFees* p2 = nullptr;
    const ParticipantFees* p1 = nullptr;
    for (const auto& f : report.participant_fees) {
        if (f.participant_id == 2) p2 = &f;
        if (f.participant_id == 1) p1 = &f;
    }
    ASSERT_NE(p2, nullptr);
    ASSERT_NE(p1, nullptr);

    EXPECT_EQ(p2->maker_volume, 10u);
    EXPECT_EQ(p2->total_fees, -250);  // Rebate

    EXPECT_EQ(p1->taker_volume, 10u);
    EXPECT_EQ(p1->total_fees, 1000);  // Fee
}

TEST(EodProcessorTest, MultiInstrument) {
    EodProcessor eod;
    eod.record_trade(make_trade(1, 100, 50000, 10), 2, 1);
    eod.record_trade(make_trade(2, 101, 3000, 100), 2, 1);

    auto report = eod.generate_report("2026-03-25");
    EXPECT_EQ(report.total_trades, 2u);
    EXPECT_EQ(report.instrument_summaries.size(), 2u);
}

TEST(EodProcessorTest, ResetClearsState) {
    EodProcessor eod;
    eod.record_trade(make_trade(1, 100, 50000, 10), 2, 1);
    EXPECT_EQ(eod.trade_count(), 1u);

    eod.reset();
    EXPECT_EQ(eod.trade_count(), 0u);

    auto report = eod.generate_report("2026-03-26");
    EXPECT_EQ(report.total_trades, 0u);
}

TEST(EodProcessorTest, TextReportFormat) {
    EodProcessor eod;
    eod.record_trade(make_trade(1, 100, 50000, 10), 2, 1);

    auto report = eod.generate_report("2026-03-25");
    auto text = report.to_text();
    EXPECT_NE(text.find("END OF DAY REPORT"), std::string::npos);
    EXPECT_NE(text.find("2026-03-25"), std::string::npos);
    EXPECT_NE(text.find("Instrument 100"), std::string::npos);
}

}  // namespace
}  // namespace eod
