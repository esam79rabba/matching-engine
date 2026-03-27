#include "engine/matching.h"
#include <gtest/gtest.h>

namespace engine {
namespace {

static uint64_t g_order_id = 1;

InstrumentConfig make_peg_config(InstrumentId id = 100) {
    InstrumentConfig cfg{};
    cfg.instrument_id = id;
    cfg.tick_size = 1;
    cfg.lot_size = 1;
    cfg.min_quantity = 1;
    cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000;
    cfg.static_band_pct = 50.0;   // Wide band for peg tests
    cfg.dynamic_band_pct = 50.0;
    cfg.residual_policy = ResidualPolicy::Cancel;
    cfg.collar_ticks = 10;
    cfg.stp_mode = STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    return cfg;
}

Order* make_order(OrderPool& pool, Side side, Price price, Quantity qty,
                  OrderType type = OrderType::Limit,
                  TimeInForce tif = TimeInForce::Day) {
    Order* o = pool.allocate();
    if (!o) return nullptr;
    o->order_id = g_order_id++;
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
    o->entry_sequence = g_order_id;
    o->peg_type = PegType::None;
    o->peg_offset = 0;
    o->peg_limit = 0;
    return o;
}

Order* make_pegged_order(OrderPool& pool, Side side, Quantity qty,
                         PegType peg_type, Price offset = 0, Price limit = 0) {
    Order* o = make_order(pool, side, 0, qty, OrderType::Limit);
    if (!o) return nullptr;
    o->peg_type = peg_type;
    o->peg_offset = offset;
    o->peg_limit = limit;
    return o;
}

// Seed the book with a bid and ask to establish a BBO
void seed_bbo(MatchingEngine& engine, OrderPool& pool,
              Price bid_price, Price ask_price, Quantity qty = 100) {
    Order* bid = make_order(pool, Side::Buy, bid_price, qty);
    engine.process_new_order(bid);

    Order* ask = make_order(pool, Side::Sell, ask_price, qty);
    engine.process_new_order(ask);
}

// ═══════════════════════════════════════════════════════════════
// Test 1: Primary peg buy follows best bid
// ═══════════════════════════════════════════════════════════════

TEST(PeggedOrderTest, PrimaryPegBuyFollowsBestBid) {
    g_order_id = 1;
    OrderPool pool(100);
    auto cfg = make_peg_config();
    MatchingEngine engine(cfg, pool);

    // Seed: bid@9900, ask@10100
    seed_bbo(engine, pool, 9900, 10100);

    EXPECT_EQ(engine.book().best_bid(), 9900);
    EXPECT_EQ(engine.book().best_ask(), 10100);

    // Primary peg buy: should peg to best_bid = 9900
    Order* peg = make_pegged_order(pool, Side::Buy, 50, PegType::Primary);
    auto result = engine.process_new_order(peg);

    EXPECT_EQ(result.action, MatchResult::Action::Inserted);
    EXPECT_TRUE(result.order_resting);

    // The pegged order should have been placed at best_bid = 9900
    Order* found = engine.book().find_order(peg->order_id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->price, 9900);
}

// ═══════════════════════════════════════════════════════════════
// Test 2: Primary peg sell follows best ask
// ═══════════════════════════════════════════════════════════════

TEST(PeggedOrderTest, PrimaryPegSellFollowsBestAsk) {
    g_order_id = 1;
    OrderPool pool(100);
    auto cfg = make_peg_config();
    MatchingEngine engine(cfg, pool);

    // Seed: bid@9900, ask@10100
    seed_bbo(engine, pool, 9900, 10100);

    // Primary peg sell: should peg to best_ask = 10100
    Order* peg = make_pegged_order(pool, Side::Sell, 50, PegType::Primary);
    auto result = engine.process_new_order(peg);

    EXPECT_EQ(result.action, MatchResult::Action::Inserted);

    Order* found = engine.book().find_order(peg->order_id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->price, 10100);
}

// ═══════════════════════════════════════════════════════════════
// Test 3: Midpoint peg
// ═══════════════════════════════════════════════════════════════

TEST(PeggedOrderTest, MidpointPeg) {
    g_order_id = 1;
    OrderPool pool(100);
    auto cfg = make_peg_config();
    MatchingEngine engine(cfg, pool);

    // Seed: bid@9900, ask@10100 → midpoint = 10000
    seed_bbo(engine, pool, 9900, 10100);

    Order* peg = make_pegged_order(pool, Side::Buy, 50, PegType::Midpoint);
    auto result = engine.process_new_order(peg);

    EXPECT_EQ(result.action, MatchResult::Action::Inserted);

    Order* found = engine.book().find_order(peg->order_id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->price, 10000);  // (9900 + 10100) / 2
}

// ═══════════════════════════════════════════════════════════════
// Test 4: Market peg (opposite side)
// ═══════════════════════════════════════════════════════════════

TEST(PeggedOrderTest, MarketPeg) {
    g_order_id = 1;
    OrderPool pool(100);
    auto cfg = make_peg_config();
    MatchingEngine engine(cfg, pool);

    // Seed: bid@9900, ask@10100
    seed_bbo(engine, pool, 9900, 10100);

    // Market peg buy: pegs to best_ask = 10100
    // This would cross, so it will match. Let's use an offset to avoid crossing.
    // Actually a market peg buy at best_ask would match the ask.
    // Use a sell-side market peg instead: sell pegged to best_bid = 9900
    Order* peg = make_pegged_order(pool, Side::Sell, 50, PegType::Market);
    auto result = engine.process_new_order(peg);

    // Market peg sell pegs to best_bid = 9900. This would cross with the bid at 9900.
    // So it will match. Let's verify fills occurred.
    EXPECT_FALSE(result.fills.empty());

    // Also test buy-side market peg with negative offset to avoid crossing
    Order* peg_buy = make_pegged_order(pool, Side::Buy, 50, PegType::Market, -300);
    auto result2 = engine.process_new_order(peg_buy);

    // best_ask was 10100, offset -300 → price = 9800 (below best_ask, should rest)
    EXPECT_EQ(result2.action, MatchResult::Action::Inserted);
    Order* found = engine.book().find_order(peg_buy->order_id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->price, 9800);  // 10100 - 300
}

// ═══════════════════════════════════════════════════════════════
// Test 5: Peg with offset
// ═══════════════════════════════════════════════════════════════

TEST(PeggedOrderTest, PegWithOffset) {
    g_order_id = 1;
    OrderPool pool(100);
    auto cfg = make_peg_config();
    MatchingEngine engine(cfg, pool);

    // Seed: bid@9900, ask@10100
    seed_bbo(engine, pool, 9900, 10100);

    // Primary peg buy with offset -50: best_bid(9900) + (-50) = 9850
    Order* peg = make_pegged_order(pool, Side::Buy, 50, PegType::Primary, -50);
    auto result = engine.process_new_order(peg);

    EXPECT_EQ(result.action, MatchResult::Action::Inserted);

    Order* found = engine.book().find_order(peg->order_id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->price, 9850);
}

// ═══════════════════════════════════════════════════════════════
// Test 6: Peg with limit (cap price)
// ═══════════════════════════════════════════════════════════════

TEST(PeggedOrderTest, PegWithLimit) {
    g_order_id = 1;
    OrderPool pool(100);
    auto cfg = make_peg_config();
    MatchingEngine engine(cfg, pool);

    // Seed: bid@9900, ask@10100
    seed_bbo(engine, pool, 9900, 10100);

    // Primary peg buy with offset +200 would give 10100,
    // but limit caps at 9950
    Order* peg = make_pegged_order(pool, Side::Buy, 50, PegType::Primary, 200, 9950);
    auto result = engine.process_new_order(peg);

    EXPECT_EQ(result.action, MatchResult::Action::Inserted);

    Order* found = engine.book().find_order(peg->order_id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->price, 9950);  // Clamped to peg_limit

    // Sell peg with limit: Primary peg sell with offset -200 would give 9900,
    // but limit floors at 10000
    Order* peg_sell = make_pegged_order(pool, Side::Sell, 50, PegType::Primary, -200, 10000);
    auto result2 = engine.process_new_order(peg_sell);

    EXPECT_EQ(result2.action, MatchResult::Action::Inserted);

    Order* found2 = engine.book().find_order(peg_sell->order_id);
    ASSERT_NE(found2, nullptr);
    EXPECT_EQ(found2->price, 10000);  // Clamped to peg_limit
}

// ═══════════════════════════════════════════════════════════════
// Test 7: Peg reprices after trade changes BBO
// ═══════════════════════════════════════════════════════════════

TEST(PeggedOrderTest, PegRepricesAfterTrade) {
    g_order_id = 1;
    OrderPool pool(200);
    auto cfg = make_peg_config();
    MatchingEngine engine(cfg, pool);

    // Seed: bid@9900, ask@10100
    seed_bbo(engine, pool, 9900, 10100);

    // Add a second ask level at 10200 (will become best ask after trade)
    Order* ask2 = make_order(pool, Side::Sell, 10200, 100);
    engine.process_new_order(ask2);

    // Midpoint peg buy: midpoint of (9900, 10100) = 10000
    Order* peg = make_pegged_order(pool, Side::Buy, 50, PegType::Midpoint);
    auto peg_result = engine.process_new_order(peg);
    EXPECT_EQ(peg_result.action, MatchResult::Action::Inserted);

    Order* peg_before = engine.book().find_order(peg->order_id);
    ASSERT_NE(peg_before, nullptr);
    EXPECT_EQ(peg_before->price, 10000);

    // Now send an aggressive buy that takes out the best ask at 10100
    // This changes best_ask to 10200.
    // best_bid = 10000 (the peg order itself), so new midpoint = (10000+10200)/2 = 10100
    Order* aggressive_buy = make_order(pool, Side::Buy, 10100, 100);
    auto trade_result = engine.process_new_order(aggressive_buy);

    EXPECT_FALSE(trade_result.fills.empty());
    EXPECT_EQ(engine.book().best_ask(), 10200);

    // The pegged order should have been repriced to new midpoint = 10100
    Order* peg_after = engine.book().find_order(peg->order_id);
    ASSERT_NE(peg_after, nullptr);
    EXPECT_EQ(peg_after->price, 10100);
}

// ═══════════════════════════════════════════════════════════════
// Test 8: Peg rejected when no reference (empty book)
// ═══════════════════════════════════════════════════════════════

TEST(PeggedOrderTest, PegRejectWhenNoReference) {
    g_order_id = 1;
    OrderPool pool(100);
    auto cfg = make_peg_config();
    MatchingEngine engine(cfg, pool);

    // Empty book — no BBO available
    Order* peg = make_pegged_order(pool, Side::Buy, 50, PegType::Primary);
    auto result = engine.process_new_order(peg);

    EXPECT_EQ(result.action, MatchResult::Action::Rejected);
    EXPECT_EQ(result.reject_code, RejectCode::PegNoReference);
}

}  // namespace
}  // namespace engine
