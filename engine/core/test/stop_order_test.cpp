#include "engine/matching.h"
#include <gtest/gtest.h>

namespace engine {
namespace {

InstrumentConfig make_config(InstrumentId id = 100) {
    InstrumentConfig cfg{};
    cfg.instrument_id = id;
    cfg.tick_size = 1;
    cfg.lot_size = 1;
    cfg.min_quantity = 1;
    cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000;
    cfg.static_band_pct = 20.0;
    cfg.dynamic_band_pct = 10.0;
    cfg.residual_policy = ResidualPolicy::Cancel;
    cfg.collar_ticks = 10;
    cfg.stp_mode = STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    return cfg;
}

Order* make_and_populate(OrderPool& pool, Side side, Price price, Quantity qty,
                         OrderType type = OrderType::Limit,
                         TimeInForce tif = TimeInForce::Day,
                         Price stop_price = 0) {
    Order* o = pool.allocate();
    if (!o) return nullptr;
    o->instrument_id = 100;
    o->side = side;
    o->order_type = type;
    o->time_in_force = tif;
    o->price = price;
    o->original_qty = qty;
    o->remaining_qty = qty;
    o->participant_id = 1;
    o->account_id = 1;
    o->session_id = 1;
    o->entry_sequence = 1;
    o->stop_price = stop_price;
    return o;
}

// ─── Test 1: Stop buy triggers when price rises ──────────────

TEST(StopOrderTest, StopBuyTriggersWhenPriceRises) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Step 1: Rest a sell at 10100 (provides liquidity)
    Order* sell = make_and_populate(pool, Side::Sell, 10100, 50);
    sell->order_id = 1;
    auto r1 = engine.process_new_order(sell);
    EXPECT_EQ(r1.action, MatchResult::Action::Inserted);

    // Step 2: Add a buy stop order with stop_price=10050
    // This should trigger when trade price >= 10050
    Order* stop_buy = make_and_populate(pool, Side::Buy, 0, 30,
                                         OrderType::Stop, TimeInForce::Day, 10050);
    stop_buy->order_id = 2;
    auto r2 = engine.process_new_order(stop_buy);
    EXPECT_EQ(r2.action, MatchResult::Action::Inserted);
    EXPECT_EQ(engine.stop_queue().count(), 1u);

    // Step 3: Rest another sell at 10200 to be consumed by the triggered stop
    Order* sell2 = make_and_populate(pool, Side::Sell, 10200, 30);
    sell2->order_id = 3;
    engine.process_new_order(sell2);

    // Step 4: Submit a buy that crosses the sell at 10100 → trade at 10100
    // This trade price (10100) >= stop_price (10050), so the stop should trigger
    Order* buy = make_and_populate(pool, Side::Buy, 10100, 50);
    buy->order_id = 4;
    auto r3 = engine.process_new_order(buy);

    // The buy matched the sell at 10100, generating a trade
    EXPECT_FALSE(r3.fills.empty());
    EXPECT_EQ(r3.fills[0].fill_price, 10100);

    // The stop order should have been triggered and activated as a market order
    // It should have consumed the sell at 10200 (market order sweeps the book)
    EXPECT_EQ(engine.stop_queue().count(), 0u);
}

// ─── Test 2: Stop sell triggers when price drops ─────────────

TEST(StopOrderTest, StopSellTriggersWhenPriceDrops) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Step 1: Rest a buy at 9900
    Order* buy = make_and_populate(pool, Side::Buy, 9900, 50);
    buy->order_id = 1;
    engine.process_new_order(buy);

    // Step 2: Add a sell stop with stop_price=9950
    // This triggers when trade price <= 9950
    Order* stop_sell = make_and_populate(pool, Side::Sell, 0, 30,
                                          OrderType::Stop, TimeInForce::Day, 9950);
    stop_sell->order_id = 2;
    auto r2 = engine.process_new_order(stop_sell);
    EXPECT_EQ(r2.action, MatchResult::Action::Inserted);
    EXPECT_EQ(engine.stop_queue().count(), 1u);

    // Step 3: Rest another buy at 9800 to be consumed by the triggered stop
    Order* buy2 = make_and_populate(pool, Side::Buy, 9800, 30);
    buy2->order_id = 3;
    engine.process_new_order(buy2);

    // Step 4: Submit a sell that crosses the buy at 9900 → trade at 9900
    // Sells match against best bid first. Best bid is 9900.
    // Trade price (9900) <= stop_price (9950), triggers the stop
    Order* sell = make_and_populate(pool, Side::Sell, 9800, 50);
    sell->order_id = 4;
    auto r3 = engine.process_new_order(sell);

    EXPECT_FALSE(r3.fills.empty());
    // First fill is at best bid 9900
    EXPECT_EQ(r3.fills[0].fill_price, 9900);

    // Stop should have triggered
    EXPECT_EQ(engine.stop_queue().count(), 0u);
}

// ─── Test 3: StopLimit rests at limit price ──────────────────

TEST(StopOrderTest, StopLimitRestsAtLimitPrice) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Step 1: Rest a sell at 10100
    Order* sell = make_and_populate(pool, Side::Sell, 10100, 50);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // Step 2: Add a buy stop-limit with stop_price=10050, limit_price=10060
    // When triggered, converts to a limit buy at 10060
    Order* stop_limit = make_and_populate(pool, Side::Buy, 10060, 30,
                                           OrderType::StopLimit, TimeInForce::Day, 10050);
    stop_limit->order_id = 2;
    auto r2 = engine.process_new_order(stop_limit);
    EXPECT_EQ(r2.action, MatchResult::Action::Inserted);
    EXPECT_EQ(engine.stop_queue().count(), 1u);

    // Step 3: Submit a buy that crosses the sell at 10100 → trade triggers the stop
    Order* buy = make_and_populate(pool, Side::Buy, 10100, 50);
    buy->order_id = 3;
    auto r3 = engine.process_new_order(buy);
    EXPECT_FALSE(r3.fills.empty());

    // Stop-limit should have triggered and converted to a limit order at 10060
    // Since best ask is now empty (sell was consumed), the limit order rests in book
    EXPECT_EQ(engine.stop_queue().count(), 0u);

    // The activated order should be resting as a limit buy at 10060
    EXPECT_EQ(engine.book().best_bid(), 10060);
    EXPECT_EQ(engine.book().best_bid_qty(), 30u);
}

// ─── Test 4: Cancel stop order ───────────────────────────────

TEST(StopOrderTest, CancelStopOrder) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Add a stop order
    Order* stop_buy = make_and_populate(pool, Side::Buy, 0, 30,
                                         OrderType::Stop, TimeInForce::Day, 10050);
    stop_buy->order_id = 1;
    engine.process_new_order(stop_buy);
    EXPECT_EQ(engine.stop_queue().count(), 1u);

    // Cancel it
    auto result = engine.process_cancel(1);
    EXPECT_EQ(result.action, MatchResult::Action::Cancelled);
    EXPECT_EQ(engine.stop_queue().count(), 0u);
}

// ─── Test 5: Rejects stop without stop_price ─────────────────

TEST(StopOrderTest, RejectsStopWithoutStopPrice) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Stop order with stop_price=0 should be rejected
    Order* stop_buy = make_and_populate(pool, Side::Buy, 0, 30,
                                         OrderType::Stop, TimeInForce::Day, 0);
    stop_buy->order_id = 1;
    auto result = engine.process_new_order(stop_buy);

    EXPECT_EQ(result.action, MatchResult::Action::Rejected);
    EXPECT_EQ(result.reject_code, RejectCode::StopPriceMissing);
}

// ─── Test 6: Multiple stops at same price (FIFO) ────────────

TEST(StopOrderTest, MultipleStopsAtSamePrice) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Rest sells to provide liquidity for triggered stops
    Order* sell1 = make_and_populate(pool, Side::Sell, 10050, 100);
    sell1->order_id = 10;
    engine.process_new_order(sell1);

    // Rest a sell at 10100 that will be consumed by the triggering buy
    Order* sell2 = make_and_populate(pool, Side::Sell, 10100, 50);
    sell2->order_id = 11;
    engine.process_new_order(sell2);

    // Add three buy stops at the same stop_price=10050
    Order* s1 = make_and_populate(pool, Side::Buy, 0, 10,
                                   OrderType::Stop, TimeInForce::Day, 10050);
    s1->order_id = 1;
    engine.process_new_order(s1);

    Order* s2 = make_and_populate(pool, Side::Buy, 0, 20,
                                   OrderType::Stop, TimeInForce::Day, 10050);
    s2->order_id = 2;
    engine.process_new_order(s2);

    Order* s3 = make_and_populate(pool, Side::Buy, 0, 30,
                                   OrderType::Stop, TimeInForce::Day, 10050);
    s3->order_id = 3;
    engine.process_new_order(s3);

    EXPECT_EQ(engine.stop_queue().count(), 3u);

    // Trigger with a buy that crosses sell2 at 10100
    Order* buy = make_and_populate(pool, Side::Buy, 10100, 50);
    buy->order_id = 4;
    engine.process_new_order(buy);

    // All three stops should have triggered (FIFO order preserved by vector)
    EXPECT_EQ(engine.stop_queue().count(), 0u);
}

// ─── Test 7: Stop does not trigger when price does not reach ─

TEST(StopOrderTest, StopDoesNotTriggerWhenPriceDoesNotReach) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Rest a sell at 10000
    Order* sell = make_and_populate(pool, Side::Sell, 10000, 50);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // Add a buy stop with stop_price=10200 (very high trigger)
    Order* stop_buy = make_and_populate(pool, Side::Buy, 0, 30,
                                         OrderType::Stop, TimeInForce::Day, 10200);
    stop_buy->order_id = 2;
    engine.process_new_order(stop_buy);
    EXPECT_EQ(engine.stop_queue().count(), 1u);

    // Buy at 10000 — trade at 10000, which is below stop_price=10200
    Order* buy = make_and_populate(pool, Side::Buy, 10000, 50);
    buy->order_id = 3;
    auto r = engine.process_new_order(buy);

    // Trade occurred at 10000
    EXPECT_FALSE(r.fills.empty());
    EXPECT_EQ(r.fills[0].fill_price, 10000);

    // Stop should NOT have triggered
    EXPECT_EQ(engine.stop_queue().count(), 1u);
}

}  // namespace
}  // namespace engine
