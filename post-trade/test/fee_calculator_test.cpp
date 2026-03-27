#include "posttrade/fee_calculator.h"

#include <gtest/gtest.h>

namespace {

using posttrade::FeeCalculator;
using posttrade::FeeSchedule;

TEST(FeeCalculator, MakerTakerFeeCalculation) {
    FeeCalculator calc;
    FeeSchedule sched{.maker_fee_bps = 10, .taker_fee_bps = 20};
    calc.set_schedule(1, sched);

    // notional = 1000 * 100 = 100000
    // maker_fee = 100000 * 10 / 10000 = 100
    // taker_fee = 100000 * 20 / 10000 = 200
    auto [maker, taker] = calc.calculate(1, 1000, 100, engine::Side::Buy);
    EXPECT_EQ(maker, 100);
    EXPECT_EQ(taker, 200);
}

TEST(FeeCalculator, DefaultScheduleUsed) {
    FeeCalculator calc;
    FeeSchedule def{.maker_fee_bps = 5, .taker_fee_bps = 15};
    calc.set_default_schedule(def);

    // Instrument 99 has no specific schedule, uses default.
    // notional = 2000 * 50 = 100000
    // maker_fee = 100000 * 5 / 10000 = 50
    // taker_fee = 100000 * 15 / 10000 = 150
    auto [maker, taker] = calc.calculate(99, 2000, 50, engine::Side::Sell);
    EXPECT_EQ(maker, 50);
    EXPECT_EQ(taker, 150);
}

TEST(FeeCalculator, InstrumentSpecificSchedule) {
    FeeCalculator calc;
    FeeSchedule def{.maker_fee_bps = 5, .taker_fee_bps = 15};
    FeeSchedule specific{.maker_fee_bps = 2, .taker_fee_bps = 8};
    calc.set_default_schedule(def);
    calc.set_schedule(42, specific);

    // Instrument 42 uses specific schedule.
    // notional = 500 * 200 = 100000
    // maker_fee = 100000 * 2 / 10000 = 20
    // taker_fee = 100000 * 8 / 10000 = 80
    auto [maker42, taker42] = calc.calculate(42, 500, 200, engine::Side::Buy);
    EXPECT_EQ(maker42, 20);
    EXPECT_EQ(taker42, 80);

    // Instrument 1 falls back to default.
    auto [maker1, taker1] = calc.calculate(1, 500, 200, engine::Side::Buy);
    EXPECT_EQ(maker1, 50);
    EXPECT_EQ(taker1, 150);
}

TEST(FeeCalculator, ZeroFeeWhenNotConfigured) {
    FeeCalculator calc;
    // No default, no instrument schedule → zero fees.
    auto [maker, taker] = calc.calculate(1, 1000, 100, engine::Side::Buy);
    EXPECT_EQ(maker, 0);
    EXPECT_EQ(taker, 0);
}

}  // namespace
