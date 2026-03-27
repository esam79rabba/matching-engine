#include "risk/risk_service.h"
#include <gtest/gtest.h>

namespace risk {
namespace {

TEST(ReduceOnlyTest, RejectsWhenWouldIncreaseLong) {
    BasicRiskService svc;
    // No existing position — sell reduce-only should be rejected (no long to reduce)
    RiskRequest req{};
    req.participant_id = 1;
    req.instrument_id = 100;
    req.side = engine::Side::Sell;
    req.order_type = engine::OrderType::Limit;
    req.price = 10000;
    req.quantity = 50;
    req.flags = engine::FLAG_REDUCE_ONLY;

    auto resp = svc.check(req);
    EXPECT_EQ(resp.result, RiskResult::REJECT);
    EXPECT_EQ(resp.reject_code, engine::RejectCode::ReduceOnlyWouldIncrease);
}

TEST(ReduceOnlyTest, AcceptsWhenReducingLong) {
    BasicRiskService svc;
    // Create a long position first
    svc.position_tracker().on_fill(1, 100, engine::Side::Buy, 100);

    // Sell reduce-only should pass (reducing long)
    RiskRequest req{};
    req.participant_id = 1;
    req.instrument_id = 100;
    req.side = engine::Side::Sell;
    req.order_type = engine::OrderType::Limit;
    req.price = 10000;
    req.quantity = 50;
    req.flags = engine::FLAG_REDUCE_ONLY;

    auto resp = svc.check(req);
    EXPECT_EQ(resp.result, RiskResult::PASS);
}

TEST(ReduceOnlyTest, AcceptsWhenReducingShort) {
    BasicRiskService svc;
    // Create a short position
    svc.position_tracker().on_fill(1, 100, engine::Side::Sell, 100);

    // Buy reduce-only should pass (reducing short)
    RiskRequest req{};
    req.participant_id = 1;
    req.instrument_id = 100;
    req.side = engine::Side::Buy;
    req.order_type = engine::OrderType::Limit;
    req.price = 10000;
    req.quantity = 50;
    req.flags = engine::FLAG_REDUCE_ONLY;

    auto resp = svc.check(req);
    EXPECT_EQ(resp.result, RiskResult::PASS);
}

TEST(ReduceOnlyTest, NonReduceOnlyUnaffected) {
    BasicRiskService svc;
    // No position, no reduce-only flag — should pass
    RiskRequest req{};
    req.participant_id = 1;
    req.instrument_id = 100;
    req.side = engine::Side::Buy;
    req.order_type = engine::OrderType::Limit;
    req.price = 10000;
    req.quantity = 50;
    req.flags = 0;

    auto resp = svc.check(req);
    EXPECT_EQ(resp.result, RiskResult::PASS);
}

}  // namespace
}  // namespace risk
