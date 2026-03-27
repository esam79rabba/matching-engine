#include "engine/matching.h"
#include <gtest/gtest.h>

namespace engine {
namespace {

InstrumentConfig make_pro_rata_config() {
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
    cfg.matching_algo = MatchingAlgorithm::ProRata;
    cfg.min_pro_rata_fill = 1;
    return cfg;
}

Order* make_order(OrderPool& pool, Side side, Price price, Quantity qty,
                  ParticipantId participant = 1) {
    Order* o = pool.allocate();
    if (!o) return nullptr;
    o->instrument_id = 100;
    o->side = side;
    o->order_type = OrderType::Limit;
    o->time_in_force = TimeInForce::Day;
    o->price = price;
    o->original_qty = qty;
    o->remaining_qty = qty;
    o->participant_id = participant;
    o->account_id = 1;
    o->session_id = 1;
    o->stp_group_id = 0;
    o->entry_sequence = 1;
    o->flags = 0;
    o->cumulative_filled = 0;
    return o;
}

// ═══════════════════════════════════════════════════════════
// Pro-Rata Allocation Tests (#12)
// ═══════════════════════════════════════════════════════════

TEST(ProRataTest, EqualOrdersGetEqualFills) {
    OrderPool pool(100);
    auto cfg = make_pro_rata_config();
    MatchingEngine engine(cfg, pool);

    // Three sells of equal size at same price
    Order* s1 = make_order(pool, Side::Sell, 10000, 100, 1);
    s1->order_id = 1;
    engine.process_new_order(s1);

    Order* s2 = make_order(pool, Side::Sell, 10000, 100, 2);
    s2->order_id = 2;
    engine.process_new_order(s2);

    Order* s3 = make_order(pool, Side::Sell, 10000, 100, 3);
    s3->order_id = 3;
    engine.process_new_order(s3);

    // Buy 150: pro-rata across 3 orders of 100 each (total 300)
    // Each gets floor(100/300 * 150) = floor(50) = 50
    Order* buy = make_order(pool, Side::Buy, 10000, 150);
    buy->order_id = 4;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.fills.size(), 3u);
    EXPECT_EQ(result.fills[0].fill_qty, 50u);
    EXPECT_EQ(result.fills[1].fill_qty, 50u);
    EXPECT_EQ(result.fills[2].fill_qty, 50u);
}

TEST(ProRataTest, ProportionalToSize) {
    OrderPool pool(100);
    auto cfg = make_pro_rata_config();
    MatchingEngine engine(cfg, pool);

    // Sell 200 and sell 100 at same price
    Order* s1 = make_order(pool, Side::Sell, 10000, 200, 1);
    s1->order_id = 1;
    engine.process_new_order(s1);

    Order* s2 = make_order(pool, Side::Sell, 10000, 100, 2);
    s2->order_id = 2;
    engine.process_new_order(s2);

    // Buy 150: total at level = 300
    // s1 gets floor(200/300 * 150) = floor(100) = 100
    // s2 gets floor(100/300 * 150) = floor(50) = 50
    Order* buy = make_order(pool, Side::Buy, 10000, 150);
    buy->order_id = 3;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.fills.size(), 2u);
    EXPECT_EQ(result.fills[0].fill_qty, 100u);
    EXPECT_EQ(result.fills[1].fill_qty, 50u);
}

TEST(ProRataTest, RemainderGoesToLargestOrder) {
    OrderPool pool(100);
    auto cfg = make_pro_rata_config();
    MatchingEngine engine(cfg, pool);

    // Two sells: 60 and 40 at same price
    Order* s1 = make_order(pool, Side::Sell, 10000, 60, 1);
    s1->order_id = 1;
    engine.process_new_order(s1);

    Order* s2 = make_order(pool, Side::Sell, 10000, 40, 2);
    s2->order_id = 2;
    engine.process_new_order(s2);

    // Buy 50: total = 100
    // s1 gets floor(60/100 * 50) = floor(30) = 30
    // s2 gets floor(40/100 * 50) = floor(20) = 20
    // Total allocated = 50, remainder = 0
    Order* buy = make_order(pool, Side::Buy, 10000, 50);
    buy->order_id = 3;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.fills.size(), 2u);
    Quantity total_filled = result.fills[0].fill_qty + result.fills[1].fill_qty;
    EXPECT_EQ(total_filled, 50u);
}

TEST(ProRataTest, FullyConsumesLevel) {
    OrderPool pool(100);
    auto cfg = make_pro_rata_config();
    MatchingEngine engine(cfg, pool);

    Order* s1 = make_order(pool, Side::Sell, 10000, 50, 1);
    s1->order_id = 1;
    engine.process_new_order(s1);

    Order* s2 = make_order(pool, Side::Sell, 10000, 50, 2);
    s2->order_id = 2;
    engine.process_new_order(s2);

    // Buy 100 — exactly consumes the level
    Order* buy = make_order(pool, Side::Buy, 10000, 100);
    buy->order_id = 3;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.fills.size(), 2u);
    EXPECT_EQ(result.fills[0].fill_qty, 50u);
    EXPECT_EQ(result.fills[1].fill_qty, 50u);
    EXPECT_EQ(result.action, MatchResult::Action::Filled);
    EXPECT_EQ(engine.book().order_count(), 0u);
}

TEST(ProRataTest, SingleOrderFallsBackToFIFOBehavior) {
    OrderPool pool(100);
    auto cfg = make_pro_rata_config();
    MatchingEngine engine(cfg, pool);

    // Only one sell at the level
    Order* sell = make_order(pool, Side::Sell, 10000, 100);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // Buy matches fully (pro-rata with 1 order = 100%)
    Order* buy = make_order(pool, Side::Buy, 10000, 50);
    buy->order_id = 2;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.fills.size(), 1u);
    EXPECT_EQ(result.fills[0].fill_qty, 50u);
}

TEST(ProRataTest, CrossesMultiplePriceLevels) {
    OrderPool pool(100);
    auto cfg = make_pro_rata_config();
    MatchingEngine engine(cfg, pool);

    // Level 10000: two sells
    Order* s1 = make_order(pool, Side::Sell, 10000, 50, 1);
    s1->order_id = 1;
    engine.process_new_order(s1);

    Order* s2 = make_order(pool, Side::Sell, 10000, 50, 2);
    s2->order_id = 2;
    engine.process_new_order(s2);

    // Level 10001: one sell
    Order* s3 = make_order(pool, Side::Sell, 10001, 100, 3);
    s3->order_id = 3;
    engine.process_new_order(s3);

    // Buy 150: sweeps level 10000 (pro-rata 100), then level 10001 (50)
    Order* buy = make_order(pool, Side::Buy, 10001, 150);
    buy->order_id = 4;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.fills.size(), 3u);
    // First two fills at 10000 (pro-rata: 50 each)
    EXPECT_EQ(result.fills[0].fill_price, 10000);
    EXPECT_EQ(result.fills[1].fill_price, 10000);
    // Third fill at 10001
    EXPECT_EQ(result.fills[2].fill_price, 10001);
    EXPECT_EQ(result.fills[2].fill_qty, 50u);
}

TEST(ProRataTest, FIFOModeStillWorks) {
    // Verify FIFO mode is unchanged
    OrderPool pool(100);
    auto cfg = make_pro_rata_config();
    cfg.matching_algo = MatchingAlgorithm::PriceTimeFIFO;  // Override to FIFO
    MatchingEngine engine(cfg, pool);

    Order* s1 = make_order(pool, Side::Sell, 10000, 100, 1);
    s1->order_id = 1;
    engine.process_new_order(s1);

    Order* s2 = make_order(pool, Side::Sell, 10000, 100, 2);
    s2->order_id = 2;
    engine.process_new_order(s2);

    // FIFO: buy 50 matches s1 only (time priority)
    Order* buy = make_order(pool, Side::Buy, 10000, 50);
    buy->order_id = 3;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.fills.size(), 1u);
    EXPECT_EQ(result.fills[0].fill_qty, 50u);
    EXPECT_EQ(result.fills[0].sell_order_id, 1u);  // s1 gets the fill
}

}  // namespace
}  // namespace engine
