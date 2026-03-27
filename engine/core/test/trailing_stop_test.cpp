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
    cfg.static_band_pct = 50.0;
    cfg.dynamic_band_pct = 0.0;  // Disable dynamic bands for cleaner tests
    cfg.residual_policy = ResidualPolicy::Cancel;
    cfg.collar_ticks = 10;
    cfg.stp_mode = STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    return cfg;
}

Order* make_order(OrderPool& pool, Side side, Price price, Quantity qty,
                  OrderType type = OrderType::Limit,
                  TimeInForce tif = TimeInForce::Day,
                  Price stop_price = 0, Price trail_offset = 0) {
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
    o->trail_offset = trail_offset;
    return o;
}

static uint64_t g_oid = 1;

Order* submit(OrderPool& pool, MatchingEngine& engine,
              Side side, Price price, Quantity qty,
              OrderType type = OrderType::Limit,
              TimeInForce tif = TimeInForce::Day,
              Price stop_price = 0, Price trail_offset = 0) {
    Order* o = make_order(pool, side, price, qty, type, tif, stop_price, trail_offset);
    o->order_id = g_oid++;
    engine.process_new_order(o);
    return o;
}

// ─── Test 1: TrailingStopSellMovesUp ─────────────────────────
// As the market rises, the sell trailing stop price should ratchet upward.

TEST(TrailingStopTest, TrailingStopSellMovesUp) {
    g_oid = 1;
    OrderPool pool(200);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Establish initial BBO: bid 10000, ask 10100
    submit(pool, engine, Side::Buy, 10000, 100);   // bid
    submit(pool, engine, Side::Sell, 10100, 100);   // ask

    // Submit sell trailing stop with trail_offset=200
    // Initial stop = best_bid(10000) - 200 = 9800
    Order* trailing = make_order(pool, Side::Sell, 0, 50,
                                  OrderType::TrailingStop, TimeInForce::Day, 0, 200);
    trailing->order_id = g_oid++;
    auto r = engine.process_new_order(trailing);
    EXPECT_EQ(r.action, MatchResult::Action::Inserted);
    EXPECT_EQ(engine.stop_queue().count(), 1u);

    Order* in_queue = engine.stop_queue().find(trailing->order_id);
    ASSERT_NE(in_queue, nullptr);
    EXPECT_EQ(in_queue->stop_price, 9800);

    // Now move the market up: place a buy that crosses the ask at 10100
    // First add more sell liquidity higher
    submit(pool, engine, Side::Sell, 10300, 100);

    // Buy at 10100 → trade happens, best_bid should update
    Order* buy1 = make_order(pool, Side::Buy, 10100, 50);
    buy1->order_id = g_oid++;
    auto r2 = engine.process_new_order(buy1);
    EXPECT_FALSE(r2.fills.empty());

    // After the trade, new best_bid might be 10000 still (the original resting bid).
    // But we placed a buy at 10100 that consumed 50 from ask at 10100.
    // best_bid is still 10000. Let's rest a new higher bid.
    submit(pool, engine, Side::Buy, 10200, 100);

    // Now best_bid = 10200. The trailing stop should update:
    // ideal_stop = 10200 - 200 = 10000 > 9800 → move up
    // We need a trade to trigger update_trailing_stops.
    // Submit a sell that crosses the bid at 10200 → triggers trade + update
    submit(pool, engine, Side::Sell, 10300, 100);  // more ask liquidity
    Order* sell_cross = make_order(pool, Side::Sell, 10200, 10);
    sell_cross->order_id = g_oid++;
    auto r3 = engine.process_new_order(sell_cross);
    EXPECT_FALSE(r3.fills.empty());

    // After this trade, update_trailing_stops was called.
    // best_bid should be 10200 (still has 90 qty remaining).
    // ideal_stop = 10200 - 200 = 10000
    in_queue = engine.stop_queue().find(trailing->order_id);
    ASSERT_NE(in_queue, nullptr);
    EXPECT_EQ(in_queue->stop_price, 10000);
}

// ─── Test 2: TrailingStopSellNeverMovesDown ──────────────────
// The sell trailing stop price should never decrease.

TEST(TrailingStopTest, TrailingStopSellNeverMovesDown) {
    g_oid = 1;
    OrderPool pool(200);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Establish BBO: bid 10200, ask 10500 (wide spread, no crossing)
    submit(pool, engine, Side::Buy, 10200, 100);
    submit(pool, engine, Side::Sell, 10500, 100);

    // Sell trailing stop: trail_offset=100
    // Initial stop = best_bid(10200) - 100 = 10100
    Order* trailing = make_order(pool, Side::Sell, 0, 50,
                                  OrderType::TrailingStop, TimeInForce::Day, 0, 100);
    trailing->order_id = g_oid++;
    engine.process_new_order(trailing);

    Order* in_queue = engine.stop_queue().find(trailing->order_id);
    ASSERT_NE(in_queue, nullptr);
    Price initial_stop = in_queue->stop_price;
    EXPECT_EQ(initial_stop, 10100);

    // Simulate market drop: a sell crosses the 10200 bid, consuming it.
    // After this, best_bid drops to nothing (or 0).
    Order* sell_cross = make_order(pool, Side::Sell, 10200, 100);
    sell_cross->order_id = g_oid++;
    auto r = engine.process_new_order(sell_cross);
    EXPECT_FALSE(r.fills.empty());

    // Now add a lower bid to establish new best_bid at 9900
    submit(pool, engine, Side::Buy, 9900, 100);

    // Trigger another trade to invoke update_trailing_stops
    submit(pool, engine, Side::Buy, 10500, 10);  // crosses ask at 10500

    // best_bid is now 9900 (the 10500 bid was consumed in trade).
    // ideal_stop = 9900 - 100 = 9800 < 10100 → should NOT move down
    in_queue = engine.stop_queue().find(trailing->order_id);
    ASSERT_NE(in_queue, nullptr);
    EXPECT_GE(in_queue->stop_price, initial_stop);
}

// ─── Test 3: TrailingStopBuyMovesDown ────────────────────────
// As the market falls, the buy trailing stop price should ratchet downward.

TEST(TrailingStopTest, TrailingStopBuyMovesDown) {
    g_oid = 1;
    OrderPool pool(200);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Establish BBO: bid 9500, ask 10100 (wide spread, no crossing)
    submit(pool, engine, Side::Buy, 9500, 100);
    submit(pool, engine, Side::Sell, 10100, 100);

    // Buy trailing stop: trail_offset=200
    // Initial stop = best_ask(10100) + 200 = 10300
    Order* trailing = make_order(pool, Side::Buy, 0, 50,
                                  OrderType::TrailingStop, TimeInForce::Day, 0, 200);
    trailing->order_id = g_oid++;
    engine.process_new_order(trailing);

    Order* in_queue = engine.stop_queue().find(trailing->order_id);
    ASSERT_NE(in_queue, nullptr);
    EXPECT_EQ(in_queue->stop_price, 10300);

    // Add a lower ask at 9900 (still above best_bid 9500, so no cross)
    submit(pool, engine, Side::Sell, 9900, 100);

    // Now best_ask = 9900. We need a trade to invoke update_trailing_stops.
    // Buy at 9900 to cross the new ask → trade at 9900
    Order* buy_cross = make_order(pool, Side::Buy, 9900, 10);
    buy_cross->order_id = g_oid++;
    auto r = engine.process_new_order(buy_cross);
    EXPECT_FALSE(r.fills.empty());

    // After trade, best_ask = 9900 (90 remaining from the 9900 sell).
    // ideal_stop = 9900 + 200 = 10100 < 10300 → move DOWN
    in_queue = engine.stop_queue().find(trailing->order_id);
    ASSERT_NE(in_queue, nullptr);
    EXPECT_EQ(in_queue->stop_price, 10100);
}

// ─── Test 4: TrailingStopTriggersOnReversal ──────────────────
// After trailing, the stop triggers when price reverses past it.

TEST(TrailingStopTest, TrailingStopTriggersOnReversal) {
    g_oid = 1;
    OrderPool pool(200);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Establish initial BBO: bid 10000, ask 10500 (wide spread)
    submit(pool, engine, Side::Buy, 10000, 100);
    submit(pool, engine, Side::Sell, 10500, 200);

    // Sell trailing stop: trail_offset=50
    // Initial stop = best_bid(10000) - 50 = 9950
    Order* trailing = make_order(pool, Side::Sell, 0, 30,
                                  OrderType::TrailingStop, TimeInForce::Day, 0, 50);
    trailing->order_id = g_oid++;
    engine.process_new_order(trailing);
    EXPECT_EQ(engine.stop_queue().count(), 1u);

    Order* in_queue = engine.stop_queue().find(trailing->order_id);
    ASSERT_NE(in_queue, nullptr);
    EXPECT_EQ(in_queue->stop_price, 9950);

    // We need buy liquidity for when the triggered stop fills as market sell
    submit(pool, engine, Side::Buy, 9800, 50);

    // Create a trade at 9950 to trigger the trailing stop.
    // Add a bid at 9950 and sell into it.
    submit(pool, engine, Side::Buy, 9950, 10);
    // Sell at 9950 crosses bid at 10000 (best bid!) → trade at 10000, not 9950.
    // We need the sell price to only match the 9950 bid.
    // Actually, sell at 9950 will match best_bid (10000) first. Trade at 10000.
    // Trade at 10000 → check_stop_triggers(10000): sell stops with stop_price >= 10000?
    // Our stop_price is 9950 < 10000, so NOT triggered.
    // But after the 10000 bid is consumed (100 qty), best_bid = 9950.
    // The sell at 9950 has remaining after matching 10000 bid... wait, the sell
    // has qty 10, and bid at 10000 has qty 100. So sell fully fills at 10000.
    // Trade at 10000 >= 9950? No, sell stops trigger when trade_price <= stop_price.
    // 10000 <= 9950 is false. Not triggered.

    // Let's use a different approach: sell enough to consume the 10000 bid,
    // then create a lower trade.
    Order* sell_consume = make_order(pool, Side::Sell, 10000, 100);
    sell_consume->order_id = g_oid++;
    auto r1 = engine.process_new_order(sell_consume);
    EXPECT_FALSE(r1.fills.empty());
    // Now bid at 10000 is consumed. best_bid = 9950.
    // Trade was at 10000. check_stop_triggers(10000): 9950 >= 10000? No.

    // Now sell into the 9950 bid → trade at 9950
    Order* sell_trigger = make_order(pool, Side::Sell, 9950, 10);
    sell_trigger->order_id = g_oid++;
    auto r2 = engine.process_new_order(sell_trigger);
    EXPECT_FALSE(r2.fills.empty());
    EXPECT_EQ(r2.fills[0].fill_price, 9950);

    // Trade at 9950 → check_stop_triggers(9950):
    // sell stops with stop_price >= 9950? Our stop has stop_price=9950.
    // 9950 >= 9950 → YES, triggered!
    // The trailing stop becomes a market sell and fills against 9800 bid.
    EXPECT_EQ(engine.stop_queue().count(), 0u);
}

// ─── Test 5: TrailingStopInitialPriceFromBBO ─────────────────
// The initial stop price is calculated from the current BBO.

TEST(TrailingStopTest, TrailingStopInitialPriceFromBBO) {
    g_oid = 1;
    OrderPool pool(200);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Establish BBO: bid 10000, ask 10200
    submit(pool, engine, Side::Buy, 10000, 100);
    submit(pool, engine, Side::Sell, 10200, 100);

    // Sell trailing stop: trail_offset=300
    // Initial stop = best_bid(10000) - 300 = 9700
    Order* sell_trail = make_order(pool, Side::Sell, 0, 50,
                                    OrderType::TrailingStop, TimeInForce::Day, 0, 300);
    sell_trail->order_id = g_oid++;
    engine.process_new_order(sell_trail);

    Order* sq = engine.stop_queue().find(sell_trail->order_id);
    ASSERT_NE(sq, nullptr);
    EXPECT_EQ(sq->stop_price, 9700);

    // Buy trailing stop: trail_offset=400
    // Initial stop = best_ask(10200) + 400 = 10600
    Order* buy_trail = make_order(pool, Side::Buy, 0, 50,
                                   OrderType::TrailingStop, TimeInForce::Day, 0, 400);
    buy_trail->order_id = g_oid++;
    engine.process_new_order(buy_trail);

    Order* bq = engine.stop_queue().find(buy_trail->order_id);
    ASSERT_NE(bq, nullptr);
    EXPECT_EQ(bq->stop_price, 10600);
}

}  // namespace
}  // namespace engine
