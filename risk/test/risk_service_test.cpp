#include "risk/risk_service.h"
#include <gtest/gtest.h>

namespace risk {
namespace {

RiskRequest make_request(engine::Price price = 10000, engine::Quantity qty = 100,
                          engine::OrderType type = engine::OrderType::Limit) {
    RiskRequest req{};
    req.client_order_id = 1;
    req.instrument_id = 100;
    req.participant_id = 1;
    req.account_id = 1;
    req.session_id = 1;
    req.side = engine::Side::Buy;
    req.order_type = type;
    req.price = price;
    req.quantity = qty;
    req.reference_price = 10000;
    req.price_multiplier = 100;  // price 10000 = $100.00, qty 100 → notional = $10,000
    return req;
}

TEST(BasicRiskServiceTest, PassesValidOrder) {
    BasicRiskService svc;
    auto resp = svc.check(make_request());
    EXPECT_EQ(resp.result, RiskResult::PASS);
}

TEST(BasicRiskServiceTest, RejectsQuantityExceedingMax) {
    BasicRiskService svc;
    RiskLimits limits;
    limits.max_order_qty = 50;
    svc.set_default_limits(limits);

    auto resp = svc.check(make_request(10000, 100));  // qty=100 > max=50
    EXPECT_EQ(resp.result, RiskResult::REJECT);
    EXPECT_EQ(resp.reject_code, engine::RejectCode::QuantityExceedsMax);
}

TEST(BasicRiskServiceTest, RejectsExcessiveNotional) {
    BasicRiskService svc;
    RiskLimits limits;
    limits.max_notional = 50.0;  // $50 max
    svc.set_default_limits(limits);

    // price=10000, qty=100, multiplier=100 → notional = 10000*100/100 = $10,000
    auto resp = svc.check(make_request(10000, 100));
    EXPECT_EQ(resp.result, RiskResult::REJECT);
    EXPECT_EQ(resp.reject_code, engine::RejectCode::RiskCheckFailed);
}

TEST(BasicRiskServiceTest, RejectsPriceDeviation) {
    BasicRiskService svc;
    RiskLimits limits;
    limits.max_price_deviation_pct = 10.0;  // 10% max
    svc.set_default_limits(limits);

    // ref=10000, price=12000 → 20% deviation > 10%
    auto resp = svc.check(make_request(12000, 100));
    EXPECT_EQ(resp.result, RiskResult::REJECT);
    EXPECT_EQ(resp.reject_code, engine::RejectCode::PriceOutsideBands);
}

TEST(BasicRiskServiceTest, PriceDeviationPassesWithinBand) {
    BasicRiskService svc;
    RiskLimits limits;
    limits.max_price_deviation_pct = 10.0;
    svc.set_default_limits(limits);

    // ref=10000, price=10500 → 5% deviation < 10%
    auto resp = svc.check(make_request(10500, 100));
    EXPECT_EQ(resp.result, RiskResult::PASS);
}

TEST(BasicRiskServiceTest, MarketOrderSkipsPriceCheck) {
    BasicRiskService svc;
    RiskLimits limits;
    limits.max_price_deviation_pct = 1.0;  // Very tight
    svc.set_default_limits(limits);

    auto req = make_request(0, 100, engine::OrderType::Market);
    auto resp = svc.check(req);
    EXPECT_EQ(resp.result, RiskResult::PASS);  // Market orders skip price check
}

TEST(BasicRiskServiceTest, KillSwitchBlocksAllOrders) {
    BasicRiskService svc;
    svc.block_participant(1);

    auto resp = svc.check(make_request());
    EXPECT_EQ(resp.result, RiskResult::REJECT);
    EXPECT_EQ(resp.reject_code, engine::RejectCode::RiskCheckFailed);
}

TEST(BasicRiskServiceTest, UnblockAllowsOrders) {
    BasicRiskService svc;
    svc.block_participant(1);
    svc.unblock_participant(1);

    auto resp = svc.check(make_request());
    EXPECT_EQ(resp.result, RiskResult::PASS);
}

TEST(BasicRiskServiceTest, PerParticipantLimits) {
    BasicRiskService svc;

    RiskLimits tight;
    tight.max_order_qty = 10;
    svc.update_limits(1, tight);

    // Participant 1: tight limits → reject qty=100
    auto resp1 = svc.check(make_request(10000, 100));
    EXPECT_EQ(resp1.result, RiskResult::REJECT);

    // Participant 2: default limits → pass qty=100
    auto req2 = make_request(10000, 100);
    req2.participant_id = 2;
    auto resp2 = svc.check(req2);
    EXPECT_EQ(resp2.result, RiskResult::PASS);
}

// ─── Open Order Count Tracking Tests ──────────────────────────

TEST(BasicRiskServiceTest, OpenOrderTracking) {
    BasicRiskService svc;

    // Insert 3 orders for participant 1
    svc.on_order_inserted(1);
    svc.on_order_inserted(1);
    svc.on_order_inserted(1);
    EXPECT_EQ(svc.open_order_count(1), 3u);

    // Cancel 1
    svc.on_order_removed(1);
    EXPECT_EQ(svc.open_order_count(1), 2u);

    // Different participant is independent
    EXPECT_EQ(svc.open_order_count(2), 0u);
    svc.on_order_inserted(2);
    EXPECT_EQ(svc.open_order_count(2), 1u);
    EXPECT_EQ(svc.open_order_count(1), 2u);  // unchanged
}

TEST(BasicRiskServiceTest, OpenOrderLimit) {
    BasicRiskService svc;
    RiskLimits limits;
    limits.max_open_orders = 5;
    svc.update_limits(1, limits);

    // Insert 5 orders
    for (int i = 0; i < 5; ++i) {
        svc.on_order_inserted(1);
    }
    EXPECT_EQ(svc.open_order_count(1), 5u);

    // The 6th order should be rejected by the risk check
    EXPECT_TRUE(svc.would_exceed_open_order_limit(1));

    // Verify through the check() path
    auto req = make_request();
    req.participant_id = 1;
    auto resp = svc.check(req);
    EXPECT_EQ(resp.result, RiskResult::REJECT);

    // Remove one order, then it should pass
    svc.on_order_removed(1);
    EXPECT_EQ(svc.open_order_count(1), 4u);
    EXPECT_FALSE(svc.would_exceed_open_order_limit(1));

    auto resp2 = svc.check(req);
    EXPECT_EQ(resp2.result, RiskResult::PASS);
}

TEST(BasicRiskServiceTest, OpenOrderRemoveNeverGoesNegative) {
    BasicRiskService svc;

    // Remove without any inserts — should stay at 0
    svc.on_order_removed(1);
    EXPECT_EQ(svc.open_order_count(1), 0u);
}

}  // namespace
}  // namespace risk
