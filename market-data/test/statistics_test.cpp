#include "marketdata/statistics.h"
#include <gtest/gtest.h>

namespace marketdata {
namespace {

TEST(StatisticsTest, TracksOHLC) {
    StatisticsTracker tracker;

    // First trade sets open, high, low, close
    tracker.on_trade(1, 100, 10);
    auto s = tracker.get_stats(1);
    EXPECT_EQ(s.open_price, 100);
    EXPECT_EQ(s.high_price, 100);
    EXPECT_EQ(s.low_price, 100);
    EXPECT_EQ(s.close_price, 100);

    // Higher trade updates high and close
    tracker.on_trade(1, 110, 5);
    s = tracker.get_stats(1);
    EXPECT_EQ(s.open_price, 100);   // Unchanged
    EXPECT_EQ(s.high_price, 110);
    EXPECT_EQ(s.low_price, 100);
    EXPECT_EQ(s.close_price, 110);

    // Lower trade updates low and close
    tracker.on_trade(1, 90, 8);
    s = tracker.get_stats(1);
    EXPECT_EQ(s.open_price, 100);
    EXPECT_EQ(s.high_price, 110);
    EXPECT_EQ(s.low_price, 90);
    EXPECT_EQ(s.close_price, 90);
}

TEST(StatisticsTest, CalculatesVWAP) {
    StatisticsTracker tracker;

    // Trade 1: price=100, qty=10 → numerator += 1000
    tracker.on_trade(1, 100, 10);
    // Trade 2: price=200, qty=10 → numerator += 2000
    tracker.on_trade(1, 200, 10);

    auto s = tracker.get_stats(1);
    // VWAP = (100*10 + 200*10) / (10+10) = 3000/20 = 150
    EXPECT_EQ(s.vwap(), 150);
}

TEST(StatisticsTest, TracksCumulativeVolume) {
    StatisticsTracker tracker;

    tracker.on_trade(1, 100, 10);
    tracker.on_trade(1, 100, 20);
    tracker.on_trade(1, 100, 30);

    auto s = tracker.get_stats(1);
    EXPECT_EQ(s.volume, 60u);
    EXPECT_EQ(s.trade_count, 3u);
}

TEST(StatisticsTest, ResetClearsStats) {
    StatisticsTracker tracker;

    tracker.on_trade(1, 100, 10);
    tracker.set_reference_price(1, 99);

    // Verify non-zero before reset
    auto s = tracker.get_stats(1);
    EXPECT_NE(s.trade_count, 0u);
    EXPECT_EQ(tracker.reference_price(1), 99);

    tracker.reset(1);

    s = tracker.get_stats(1);
    EXPECT_EQ(s.open_price, 0);
    EXPECT_EQ(s.high_price, 0);
    EXPECT_EQ(s.low_price, 0);
    EXPECT_EQ(s.close_price, 0);
    EXPECT_EQ(s.volume, 0u);
    EXPECT_EQ(s.trade_count, 0u);
    EXPECT_EQ(s.vwap(), 0);
    EXPECT_EQ(tracker.reference_price(1), 0);
}

TEST(StatisticsTest, ReferenceAndClosingPrices) {
    StatisticsTracker tracker;

    // Initially zero
    EXPECT_EQ(tracker.reference_price(1), 0);
    EXPECT_EQ(tracker.opening_price(1), 0);
    EXPECT_EQ(tracker.closing_price(1), 0);

    tracker.set_reference_price(1, 1000);
    tracker.set_opening_price(1, 1005);
    tracker.set_closing_price(1, 995);

    EXPECT_EQ(tracker.reference_price(1), 1000);
    EXPECT_EQ(tracker.opening_price(1), 1005);
    EXPECT_EQ(tracker.closing_price(1), 995);

    // Different instrument is independent
    EXPECT_EQ(tracker.reference_price(2), 0);

    // reset_all clears everything
    tracker.reset_all();
    EXPECT_EQ(tracker.reference_price(1), 0);
    EXPECT_EQ(tracker.opening_price(1), 0);
    EXPECT_EQ(tracker.closing_price(1), 0);
}

}  // namespace
}  // namespace marketdata
