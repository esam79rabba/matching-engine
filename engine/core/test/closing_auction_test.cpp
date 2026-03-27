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

Order* make_order(OrderPool& pool, Side side, Price price, Quantity qty,
                  OrderType type = OrderType::Limit,
                  TimeInForce tif = TimeInForce::Day) {
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
    return o;
}

// Helper: transition engine to ClosingAuction state via Continuous
void transition_to_closing_auction(MatchingEngine& engine) {
    engine.force_state(InstrumentState::Continuous);
    ASSERT_TRUE(engine.transition_state(InstrumentState::ClosingAuction));
}

// ═══════════════════════════════════════════════════════════════
// Test 1: Orders collect without matching during ClosingAuction
// ═══════════════════════════════════════════════════════════════

TEST(ClosingAuctionTest, ClosingAuctionCollectsOrders) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);
    transition_to_closing_auction(engine);

    // Submit crossing orders — they should NOT match during ClosingAuction
    Order* buy1 = make_order(pool, Side::Buy, 10200, 100);
    buy1->order_id = 1;
    auto r1 = engine.process_new_order(buy1);
    EXPECT_EQ(r1.action, MatchResult::Action::Inserted);
    EXPECT_TRUE(r1.order_resting);
    EXPECT_TRUE(r1.fills.empty());

    Order* sell1 = make_order(pool, Side::Sell, 9800, 80);
    sell1->order_id = 2;
    auto r2 = engine.process_new_order(sell1);
    EXPECT_EQ(r2.action, MatchResult::Action::Inserted);
    EXPECT_TRUE(r2.order_resting);
    EXPECT_TRUE(r2.fills.empty());

    // Both orders should rest in the book without any trades
    EXPECT_EQ(engine.book().order_count(), 2u);
}

// ═══════════════════════════════════════════════════════════════
// Test 2: Closing auction determines volume-maximizing price
// ═══════════════════════════════════════════════════════════════

TEST(ClosingAuctionTest, ClosingAuctionDeterminesPrice) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);
    transition_to_closing_auction(engine);

    // Buy at 10200, qty 100
    Order* buy1 = make_order(pool, Side::Buy, 10200, 100);
    buy1->order_id = 1;
    engine.process_new_order(buy1);

    // Sell at 9800, qty 80
    Order* sell1 = make_order(pool, Side::Sell, 9800, 80);
    sell1->order_id = 2;
    engine.process_new_order(sell1);

    EXPECT_EQ(engine.book().order_count(), 2u);

    // Calculate auction price
    auto [price, volume] = engine.calculate_auction_price();
    EXPECT_GT(price, 0);
    EXPECT_EQ(volume, 80u);  // Limited by sell side

    // Run the closing auction
    auto result = engine.run_closing_auction();
    EXPECT_EQ(result.action, MatchResult::Action::Filled);
    EXPECT_EQ(result.fills.size(), 1u);
    EXPECT_EQ(result.fills[0].fill_qty, 80u);

    // State should be Closed
    EXPECT_EQ(engine.state(), InstrumentState::Closed);

    // Buy order should have 20 remaining
    EXPECT_EQ(engine.book().order_count(), 1u);
    EXPECT_EQ(engine.book().best_bid(), 10200);
    EXPECT_EQ(engine.book().best_bid_qty(), 20u);
}

// ═══════════════════════════════════════════════════════════════
// Test 3: Market orders are rejected during ClosingAuction
// ═══════════════════════════════════════════════════════════════

TEST(ClosingAuctionTest, ClosingAuctionRejectsMarketOrders) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);
    transition_to_closing_auction(engine);

    Order* mkt = make_order(pool, Side::Buy, 0, 50, OrderType::Market);
    mkt->order_id = 1;
    auto result = engine.process_new_order(mkt);

    EXPECT_EQ(result.action, MatchResult::Action::Rejected);
    EXPECT_EQ(result.reject_code, RejectCode::InvalidOrderType);
    EXPECT_EQ(engine.book().order_count(), 0u);
}

// ═══════════════════════════════════════════════════════════════
// Test 4: After auction, state transitions to Closed
// ═══════════════════════════════════════════════════════════════

TEST(ClosingAuctionTest, ClosingAuctionTransitionsToClosed) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);
    transition_to_closing_auction(engine);

    // Submit some crossing orders
    Order* buy1 = make_order(pool, Side::Buy, 10100, 50);
    buy1->order_id = 1;
    engine.process_new_order(buy1);

    Order* sell1 = make_order(pool, Side::Sell, 9900, 50);
    sell1->order_id = 2;
    engine.process_new_order(sell1);

    EXPECT_EQ(engine.state(), InstrumentState::ClosingAuction);

    auto result = engine.run_closing_auction();
    EXPECT_EQ(result.action, MatchResult::Action::Filled);
    EXPECT_EQ(engine.state(), InstrumentState::Closed);

    // Verify no new orders accepted after Closed
    Order* late_order = make_order(pool, Side::Buy, 10000, 10);
    late_order->order_id = 3;
    auto r2 = engine.process_new_order(late_order);
    EXPECT_EQ(r2.action, MatchResult::Action::Rejected);
    EXPECT_EQ(r2.reject_code, RejectCode::InstrumentHalted);
}

// ═══════════════════════════════════════════════════════════════
// Test 5: Empty closing auction — no orders, transitions to Closed
// ═══════════════════════════════════════════════════════════════

TEST(ClosingAuctionTest, EmptyClosingAuction) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);
    transition_to_closing_auction(engine);

    // No orders submitted
    auto result = engine.run_closing_auction();
    EXPECT_TRUE(result.fills.empty());
    EXPECT_EQ(engine.state(), InstrumentState::Closed);
}

// ═══════════════════════════════════════════════════════════════
// Test 6: Partial fills and residual order handling
// ═══════════════════════════════════════════════════════════════

TEST(ClosingAuctionTest, ClosingAuctionPartialFills) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);
    transition_to_closing_auction(engine);

    // Multiple bid levels
    Order* b1 = make_order(pool, Side::Buy, 10500, 100);
    b1->order_id = 1;
    engine.process_new_order(b1);

    Order* b2 = make_order(pool, Side::Buy, 10200, 50);
    b2->order_id = 2;
    engine.process_new_order(b2);

    Order* b3 = make_order(pool, Side::Buy, 10000, 30);
    b3->order_id = 3;
    engine.process_new_order(b3);

    // Multiple ask levels
    Order* s1 = make_order(pool, Side::Sell, 9800, 80);
    s1->order_id = 4;
    engine.process_new_order(s1);

    Order* s2 = make_order(pool, Side::Sell, 10100, 60);
    s2->order_id = 5;
    engine.process_new_order(s2);

    Order* s3 = make_order(pool, Side::Sell, 10400, 40);
    s3->order_id = 6;
    engine.process_new_order(s3);

    EXPECT_EQ(engine.book().order_count(), 6u);

    auto result = engine.run_closing_auction();
    EXPECT_EQ(result.action, MatchResult::Action::Filled);

    // Total fills should sum to the maximum executable volume
    Quantity total_filled = 0;
    for (const auto& f : result.fills) {
        total_filled += f.fill_qty;
    }
    // Same scenario as OpeningAuctionMaximizesVolume: max volume is 140
    EXPECT_EQ(total_filled, 140u);

    // State should be Closed after closing auction
    EXPECT_EQ(engine.state(), InstrumentState::Closed);

    // Residual orders should remain in the book
    EXPECT_GT(engine.book().order_count(), 0u);
}

}  // namespace
}  // namespace engine
