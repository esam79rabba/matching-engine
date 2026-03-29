#include "engine/matching.h"
#include <gtest/gtest.h>

namespace engine {
namespace {

InstrumentConfig make_config() {
    InstrumentConfig cfg{};
    cfg.instrument_id = 100;
    cfg.tick_size = 1;
    cfg.lot_size = 1;
    cfg.min_quantity = 1;
    cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000;
    cfg.static_band_pct = 50.0;
    cfg.dynamic_band_pct = 50.0;
    cfg.residual_policy = ResidualPolicy::Cancel;
    cfg.collar_ticks = 10;
    cfg.stp_mode = STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    return cfg;
}

Order* make_order(OrderPool& pool, Side side, Price price, Quantity qty,
                  TimeInForce tif = TimeInForce::Day) {
    Order* o = pool.allocate();
    if (!o) return nullptr;
    o->instrument_id = 100;
    o->side = side;
    o->order_type = OrderType::Limit;
    o->time_in_force = tif;
    o->price = price;
    o->original_qty = qty;
    o->remaining_qty = qty;
    o->display_qty = 0;
    o->cumulative_filled = 0;
    o->stop_price = 0;
    o->trail_offset = 0;
    o->peg_type = PegType::None;
    o->peg_offset = 0;
    o->peg_limit = 0;
    o->budget = 0;
    o->budget_spent = 0;
    o->participant_id = 1;
    o->account_id = 1;
    o->session_id = 1;
    o->stp_group_id = 0;
    o->entry_sequence = 1;
    o->flags = 0;
    return o;
}

TEST(BudgetOrderTest, IOCBudgetFillsAndCancelsRemainder) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Rest sells at different prices
    Order* s1 = make_order(pool, Side::Sell, 10000, 50);
    s1->order_id = 1;
    engine.process_new_order(s1);

    Order* s2 = make_order(pool, Side::Sell, 10100, 50);
    s2->order_id = 2;
    engine.process_new_order(s2);

    // IOC_BUDGET buy: budget enough for some but not all
    Order* buy = make_order(pool, Side::Buy, 10100, 100, TimeInForce::IOC_BUDGET);
    buy->order_id = 3;
    buy->budget = 600000;  // enough for ~60 units at avg price 10000
    auto result = engine.process_new_order(buy);

    // Should have filled what it could and cancelled rest
    EXPECT_GE(result.fills.size(), 1u);
    // Budget_spent should be tracked
    EXPECT_GT(buy->budget_spent, 0);
}

TEST(BudgetOrderTest, FOKBudgetRejectsIfInsufficientLiquidity) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Only 10 units available
    Order* sell = make_order(pool, Side::Sell, 10000, 10);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // FOK_BUDGET wants 100 units — should fail pre-check
    Order* buy = make_order(pool, Side::Buy, 10000, 100, TimeInForce::FOK_BUDGET);
    buy->order_id = 2;
    buy->budget = 1000000;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.action, MatchResult::Action::Rejected);
    EXPECT_EQ(result.reject_code, RejectCode::FOKInsufficientQty);
}

TEST(BudgetOrderTest, IOCBudgetTracksNotionalSpent) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Rest a sell at 10000, qty 100
    Order* sell = make_order(pool, Side::Sell, 10000, 100);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // IOC_BUDGET buy at 10000, qty 100 — should fill fully
    Order* buy = make_order(pool, Side::Buy, 10000, 100, TimeInForce::IOC_BUDGET);
    buy->order_id = 2;
    buy->budget = 1500000;  // More than enough
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.fills.size(), 1u);
    EXPECT_EQ(result.fills[0].fill_qty, 100u);
    // Notional = 10000 * 100 = 1,000,000
    EXPECT_EQ(buy->budget_spent, 1000000);
}

}  // namespace
}  // namespace engine
