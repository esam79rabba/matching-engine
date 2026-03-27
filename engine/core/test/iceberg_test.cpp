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
    cfg.static_band_pct = 20.0;
    cfg.dynamic_band_pct = 10.0;
    cfg.residual_policy = ResidualPolicy::Cancel;
    cfg.collar_ticks = 10;
    cfg.stp_mode = STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    cfg.matching_algo = MatchingAlgorithm::PriceTimeFIFO;
    cfg.min_pro_rata_fill = 1;
    return cfg;
}

Order* make_order(OrderPool& pool, Side side, Price price, Quantity qty,
                  Quantity display = 0, ParticipantId participant = 1) {
    Order* o = pool.allocate();
    if (!o) return nullptr;
    o->instrument_id = 100;
    o->side = side;
    o->order_type = OrderType::Limit;
    o->time_in_force = TimeInForce::Day;
    o->price = price;
    o->original_qty = qty;
    o->remaining_qty = qty;
    o->display_qty = display;  // 0 = fully visible (non-iceberg)
    o->cumulative_filled = 0;
    o->stop_price = 0;
    o->participant_id = participant;
    o->account_id = 1;
    o->session_id = 1;
    o->stp_group_id = 0;
    o->entry_sequence = 1;
    o->flags = (display > 0) ? FLAG_ICEBERG : 0;
    return o;
}

// ═══════════════════════════════════════════════════════════
// Basic Iceberg Order Tests
// ═══════════════════════════════════════════════════════════

TEST(IcebergTest, VisibleQtyOnlyInMarketData) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Iceberg sell: total 1000, display 100
    Order* sell = make_order(pool, Side::Sell, 10000, 1000, 100);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // Market data should show only display_qty
    EXPECT_EQ(engine.book().best_ask(), 10000);
    EXPECT_EQ(engine.book().best_ask_qty(), 100u);  // Only visible portion
    EXPECT_EQ(engine.book().order_count(), 1u);
}

TEST(IcebergTest, PartialFillWithinVisible) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Iceberg sell: total 1000, display 100
    Order* sell = make_order(pool, Side::Sell, 10000, 1000, 100);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // Buy 50 — fills within visible portion, no replenishment
    Order* buy = make_order(pool, Side::Buy, 10000, 50);
    buy->order_id = 2;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.fills.size(), 1u);
    EXPECT_EQ(result.fills[0].fill_qty, 50u);
    // Remaining: 950 total, 50 visible
    EXPECT_EQ(engine.book().best_ask_qty(), 50u);
    EXPECT_EQ(engine.book().order_count(), 1u);
}

TEST(IcebergTest, ReplenishesAfterVisibleExhausted) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Iceberg sell: total 300, display 100
    Order* sell = make_order(pool, Side::Sell, 10000, 300, 100);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // Buy 100 — exhausts visible portion → replenish
    Order* buy = make_order(pool, Side::Buy, 10000, 100);
    buy->order_id = 2;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.fills.size(), 1u);
    EXPECT_EQ(result.fills[0].fill_qty, 100u);
    // After fill: remaining=200, replenish display to min(200, 300)=200? No — replenish to original_qty=300, but capped at remaining=200
    // Actually replenish = min(remaining_qty, original_qty) = min(200, 300) = 200
    EXPECT_EQ(engine.book().best_ask_qty(), 200u);
    EXPECT_EQ(engine.book().order_count(), 1u);
}

TEST(IcebergTest, MultipleRefreshCycles) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Iceberg sell: total 300, display 100
    Order* sell = make_order(pool, Side::Sell, 10000, 300, 100);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // Cycle 1: Buy 100 → visible exhausted, replenish to min(200, 300)=200
    Order* b1 = make_order(pool, Side::Buy, 10000, 100);
    b1->order_id = 2;
    engine.process_new_order(b1);
    EXPECT_EQ(engine.book().best_ask_qty(), 200u);

    // Cycle 2: Buy 200 → fully consume remaining
    Order* b2 = make_order(pool, Side::Buy, 10000, 200);
    b2->order_id = 3;
    auto result = engine.process_new_order(b2);
    EXPECT_EQ(result.fills.size(), 1u);
    EXPECT_EQ(result.fills[0].fill_qty, 200u);
    EXPECT_EQ(engine.book().order_count(), 0u);  // Fully consumed
}

TEST(IcebergTest, LosesPriorityOnReplenish) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Iceberg sell: total 500, display 100
    Order* iceberg = make_order(pool, Side::Sell, 10000, 500, 100);
    iceberg->order_id = 1;
    engine.process_new_order(iceberg);

    // Regular sell at same price (entered after iceberg)
    Order* regular = make_order(pool, Side::Sell, 10000, 200);
    regular->order_id = 2;
    engine.process_new_order(regular);

    // Buy 100: should fill iceberg first (time priority)
    Order* b1 = make_order(pool, Side::Buy, 10000, 100);
    b1->order_id = 3;
    auto r1 = engine.process_new_order(b1);
    EXPECT_EQ(r1.fills.size(), 1u);
    EXPECT_EQ(r1.fills[0].sell_order_id, 1u);  // Iceberg filled first

    // Now iceberg replenishes and moves to back of queue.
    // Buy 100: should fill regular (now has priority)
    Order* b2 = make_order(pool, Side::Buy, 10000, 100);
    b2->order_id = 4;
    auto r2 = engine.process_new_order(b2);
    EXPECT_EQ(r2.fills.size(), 1u);
    EXPECT_EQ(r2.fills[0].sell_order_id, 2u);  // Regular gets priority now
}

TEST(IcebergTest, CancelIcebergCancelsAll) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Iceberg sell: total 1000, display 100
    Order* sell = make_order(pool, Side::Sell, 10000, 1000, 100);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // Cancel entire iceberg
    auto result = engine.process_cancel(1);
    EXPECT_EQ(result.action, MatchResult::Action::Cancelled);
    EXPECT_EQ(engine.book().order_count(), 0u);
    EXPECT_EQ(engine.book().best_ask(), 0);
}

TEST(IcebergTest, NonIcebergUnchanged) {
    // Verify regular orders (display_qty=0) work exactly as before
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    Order* sell = make_order(pool, Side::Sell, 10000, 100, 0);  // Non-iceberg
    sell->order_id = 1;
    engine.process_new_order(sell);

    EXPECT_EQ(engine.book().best_ask_qty(), 100u);

    Order* buy = make_order(pool, Side::Buy, 10000, 50, 0);
    buy->order_id = 2;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.fills.size(), 1u);
    EXPECT_EQ(result.fills[0].fill_qty, 50u);
    EXPECT_EQ(engine.book().best_ask_qty(), 50u);
}

TEST(IcebergTest, SweepEntireIceberg) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Iceberg sell: total 200, display 50
    Order* sell = make_order(pool, Side::Sell, 10000, 200, 50);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // Buy 200 — sweeps all of iceberg (visible + hidden)
    Order* buy = make_order(pool, Side::Buy, 10000, 200);
    buy->order_id = 2;
    auto result = engine.process_new_order(buy);

    // Should get multiple fills as iceberg replenishes
    Quantity total_filled = 0;
    for (const auto& f : result.fills) {
        total_filled += f.fill_qty;
    }
    EXPECT_EQ(total_filled, 200u);
    EXPECT_EQ(engine.book().order_count(), 0u);
}

}  // namespace
}  // namespace engine
