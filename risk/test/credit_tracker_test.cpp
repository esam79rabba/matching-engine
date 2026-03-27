#include "risk/credit_tracker.h"
#include <gtest/gtest.h>

namespace risk {
namespace {

TEST(CreditTrackerTest, TracksExposureOnFill) {
    CreditTracker tracker;
    tracker.on_fill(1, 100, 50);  // notional = 100 * 50 = 5000

    EXPECT_EQ(tracker.get_exposure(1), 5000);
}

TEST(CreditTrackerTest, TracksExposureOnOrderAccepted) {
    CreditTracker tracker;
    tracker.on_order_accepted(1, 200, 10);  // notional = 200 * 10 = 2000
    tracker.on_order_accepted(1, 150, 20);  // notional = 150 * 20 = 3000

    EXPECT_EQ(tracker.get_exposure(1), 5000);  // 2000 + 3000
}

TEST(CreditTrackerTest, ReleasesExposureOnCancel) {
    CreditTracker tracker;
    tracker.on_order_accepted(1, 200, 10);  // +2000
    tracker.on_order_accepted(1, 150, 20);  // +3000
    tracker.on_order_cancelled(1, 200, 10); // -2000

    EXPECT_EQ(tracker.get_exposure(1), 3000);
}

TEST(CreditTrackerTest, CheckLimitAllowsWithinCredit) {
    CreditTracker tracker;
    tracker.on_fill(1, 100, 50);  // exposure = 5000

    // 5000 + (100 * 40) = 9000 <= 10000 -> OK
    EXPECT_TRUE(tracker.check_limit(1, 100, 40, 10000));
}

TEST(CreditTrackerTest, CheckLimitRejectsOverCredit) {
    CreditTracker tracker;
    tracker.on_fill(1, 100, 50);  // exposure = 5000

    // 5000 + (100 * 60) = 11000 > 10000 -> REJECT
    EXPECT_FALSE(tracker.check_limit(1, 100, 60, 10000));
}

}  // namespace
}  // namespace risk
