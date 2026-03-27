#include "risk/position_tracker.h"
#include <gtest/gtest.h>

namespace risk {
namespace {

TEST(PositionTrackerTest, TracksBuyPosition) {
    PositionTracker tracker;
    tracker.on_fill(1, 100, engine::Side::Buy, 50);

    auto pos = tracker.get_position(1, 100);
    EXPECT_EQ(pos.net_qty, 50);
    EXPECT_EQ(pos.buy_qty, 50u);
    EXPECT_EQ(pos.sell_qty, 0u);
}

TEST(PositionTrackerTest, TracksSellPosition) {
    PositionTracker tracker;
    tracker.on_fill(1, 100, engine::Side::Sell, 30);

    auto pos = tracker.get_position(1, 100);
    EXPECT_EQ(pos.net_qty, -30);
    EXPECT_EQ(pos.buy_qty, 0u);
    EXPECT_EQ(pos.sell_qty, 30u);
}

TEST(PositionTrackerTest, NetPositionAfterBuysAndSells) {
    PositionTracker tracker;
    tracker.on_fill(1, 100, engine::Side::Buy, 100);
    tracker.on_fill(1, 100, engine::Side::Sell, 40);
    tracker.on_fill(1, 100, engine::Side::Buy, 20);

    auto pos = tracker.get_position(1, 100);
    EXPECT_EQ(pos.net_qty, 80);    // 100 - 40 + 20
    EXPECT_EQ(pos.buy_qty, 120u);  // 100 + 20
    EXPECT_EQ(pos.sell_qty, 40u);
}

TEST(PositionTrackerTest, CheckLimitAllowsWithinRange) {
    PositionTracker tracker;
    tracker.on_fill(1, 100, engine::Side::Buy, 50);

    // Current net = +50, buying 40 more -> +90, max_long = 100 -> OK
    EXPECT_TRUE(tracker.check_limit(1, 100, engine::Side::Buy, 40, 100, 100));

    // Current net = +50, selling 60 -> -10, max_short = 100 -> OK
    EXPECT_TRUE(tracker.check_limit(1, 100, engine::Side::Sell, 60, 100, 100));
}

TEST(PositionTrackerTest, CheckLimitRejectsLongBreach) {
    PositionTracker tracker;
    tracker.on_fill(1, 100, engine::Side::Buy, 80);

    // Current net = +80, buying 30 more -> +110, max_long = 100 -> REJECT
    EXPECT_FALSE(tracker.check_limit(1, 100, engine::Side::Buy, 30, 100, 100));
}

TEST(PositionTrackerTest, CheckLimitRejectsShortBreach) {
    PositionTracker tracker;
    tracker.on_fill(1, 100, engine::Side::Sell, 80);

    // Current net = -80, selling 30 more -> -110, max_short = 100 -> REJECT
    EXPECT_FALSE(tracker.check_limit(1, 100, engine::Side::Sell, 30, 100, 100));
}

}  // namespace
}  // namespace risk
