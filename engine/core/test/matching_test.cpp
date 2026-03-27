#include "engine/matching.h"
#include <gtest/gtest.h>

namespace engine {
namespace {

InstrumentConfig make_config(InstrumentId id = 100) {
    InstrumentConfig cfg{};
    cfg.instrument_id = id;
    cfg.tick_size = 1;           // Tick = 1 unit (generic)
    cfg.lot_size = 1;            // Lot = 1 unit (generic)
    cfg.min_quantity = 1;
    cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000; // Reference price
    cfg.static_band_pct = 20.0;   // 20% static band
    cfg.dynamic_band_pct = 10.0;
    cfg.residual_policy = ResidualPolicy::Cancel;
    cfg.collar_ticks = 10;
    cfg.stp_mode = STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    return cfg;
}

Order* make_and_populate(OrderPool& pool, Side side, Price price, Quantity qty,
                         OrderType type = OrderType::Limit,
                         TimeInForce tif = TimeInForce::Day) {
    // We don't actually use pool.allocate() here because MatchingEngine
    // allocates internally through Partition. For testing MatchingEngine
    // directly, we allocate from pool and pass in.
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

TEST(MatchingEngineTest, LimitOrderInserts) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    Order* buy = make_and_populate(pool, Side::Buy, 10000, 100);
    buy->order_id = 1;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.action, MatchResult::Action::Inserted);
    EXPECT_TRUE(result.order_resting);
    EXPECT_EQ(engine.book().best_bid(), 10000);
}

TEST(MatchingEngineTest, RejectsInvalidTick) {
    OrderPool pool(100);
    auto cfg = make_config();
    cfg.tick_size = 5;  // Only prices divisible by 5
    MatchingEngine engine(cfg, pool);

    Order* buy = make_and_populate(pool, Side::Buy, 10003, 100);  // Not tick-aligned
    buy->order_id = 1;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.action, MatchResult::Action::Rejected);
    EXPECT_EQ(result.reject_code, RejectCode::InvalidTick);
}

TEST(MatchingEngineTest, RejectsQuantityExceedingMax) {
    OrderPool pool(100);
    auto cfg = make_config();
    cfg.max_quantity = 500;
    MatchingEngine engine(cfg, pool);

    Order* buy = make_and_populate(pool, Side::Buy, 10000, 600);
    buy->order_id = 1;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.action, MatchResult::Action::Rejected);
    EXPECT_EQ(result.reject_code, RejectCode::QuantityExceedsMax);
}

TEST(MatchingEngineTest, RejectsPriceOutsideStaticBand) {
    OrderPool pool(100);
    auto cfg = make_config();
    cfg.static_ref_price = 10000;
    cfg.static_band_pct = 10.0;  // 10% band → 9000–11000
    MatchingEngine engine(cfg, pool);

    Order* buy = make_and_populate(pool, Side::Buy, 12000, 100);  // 20% above ref
    buy->order_id = 1;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.action, MatchResult::Action::Rejected);
    EXPECT_EQ(result.reject_code, RejectCode::PriceOutsideBands);
}

TEST(MatchingEngineTest, IOCCancelsResidual) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Resting sell of 50
    Order* sell = make_and_populate(pool, Side::Sell, 10000, 50);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // IOC buy of 100 — matches 50, residual 50 cancelled
    Order* buy = make_and_populate(pool, Side::Buy, 10000, 100, OrderType::Limit, TimeInForce::IOC);
    buy->order_id = 2;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.action, MatchResult::Action::Cancelled);
    EXPECT_EQ(result.reject_code, RejectCode::IOCResidualCancel);
    EXPECT_EQ(engine.book().order_count(), 0u);  // Nothing resting
}

TEST(MatchingEngineTest, MarketOrderResidualCancelledByDefault) {
    OrderPool pool(100);
    auto cfg = make_config();
    cfg.residual_policy = ResidualPolicy::Cancel;
    MatchingEngine engine(cfg, pool);

    // Resting sell of 50
    Order* sell = make_and_populate(pool, Side::Sell, 10000, 50);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // Market buy of 100 — matches 50, residual 50 cancelled per policy
    Order* buy = make_and_populate(pool, Side::Buy, 0, 100, OrderType::Market);
    buy->order_id = 2;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.action, MatchResult::Action::Cancelled);
    EXPECT_EQ(result.reject_code, RejectCode::MarketResidualCancel);
}

TEST(MatchingEngineTest, CancelExistingOrder) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    Order* buy = make_and_populate(pool, Side::Buy, 10000, 100);
    buy->order_id = 1;
    engine.process_new_order(buy);

    auto result = engine.process_cancel(1);
    EXPECT_EQ(result.action, MatchResult::Action::Cancelled);
    EXPECT_EQ(engine.book().order_count(), 0u);
}

TEST(MatchingEngineTest, CancelUnknownOrderRejected) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    auto result = engine.process_cancel(999);
    EXPECT_EQ(result.action, MatchResult::Action::Rejected);
}

// ─── FOK Pre-Check Tests ─────────────────────────────────────

TEST(MatchingEngineTest, FOKFullyFillable) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Place resting sells totaling 100
    Order* sell1 = make_and_populate(pool, Side::Sell, 10000, 60);
    sell1->order_id = 1;
    engine.process_new_order(sell1);

    Order* sell2 = make_and_populate(pool, Side::Sell, 10000, 40);
    sell2->order_id = 2;
    engine.process_new_order(sell2);

    // FOK buy for 100 — sufficient liquidity at price 10000
    Order* fok = make_and_populate(pool, Side::Buy, 10000, 100, OrderType::Limit, TimeInForce::FOK);
    fok->order_id = 3;
    auto result = engine.process_new_order(fok);

    EXPECT_EQ(result.action, MatchResult::Action::Filled);
    EXPECT_EQ(result.fills.size(), 2u);
}

TEST(MatchingEngineTest, FOKInsufficientLiquidity) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // No resting orders — empty book
    Order* fok = make_and_populate(pool, Side::Buy, 10000, 100, OrderType::Limit, TimeInForce::FOK);
    fok->order_id = 1;
    auto result = engine.process_new_order(fok);

    EXPECT_EQ(result.action, MatchResult::Action::Rejected);
    EXPECT_EQ(result.reject_code, RejectCode::FOKInsufficientQty);
}

TEST(MatchingEngineTest, FOKPartialLiquidityRejected) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Only 50 available
    Order* sell = make_and_populate(pool, Side::Sell, 10000, 50);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // FOK buy for 100 — only 50 available → rejected
    Order* fok = make_and_populate(pool, Side::Buy, 10000, 100, OrderType::Limit, TimeInForce::FOK);
    fok->order_id = 2;
    auto result = engine.process_new_order(fok);

    EXPECT_EQ(result.action, MatchResult::Action::Rejected);
    EXPECT_EQ(result.reject_code, RejectCode::FOKInsufficientQty);
    // Verify the resting sell is still intact (no partial mutation)
    EXPECT_EQ(engine.book().order_count(), 1u);
    EXPECT_EQ(engine.book().best_ask_qty(), 50u);
}

// ─── Opening Auction Tests ──────────────────────────────────

TEST(MatchingEngineTest, PreOpenAcceptsOrdersWithoutMatching) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Transition to PreOpen
    engine.force_state(InstrumentState::Inactive);
    ASSERT_TRUE(engine.transition_state(InstrumentState::PreOpen));
    EXPECT_EQ(engine.state(), InstrumentState::PreOpen);

    // Submit buy and sell that would cross in continuous trading
    Order* buy = make_and_populate(pool, Side::Buy, 10100, 50);
    buy->order_id = 1;
    auto r1 = engine.process_new_order(buy);
    EXPECT_EQ(r1.action, MatchResult::Action::Inserted);
    EXPECT_TRUE(r1.order_resting);

    Order* sell = make_and_populate(pool, Side::Sell, 9900, 50);
    sell->order_id = 2;
    auto r2 = engine.process_new_order(sell);
    EXPECT_EQ(r2.action, MatchResult::Action::Inserted);
    EXPECT_TRUE(r2.order_resting);

    // Both orders should be resting — no trades generated
    EXPECT_EQ(r1.fills.size(), 0u);
    EXPECT_EQ(r2.fills.size(), 0u);
    EXPECT_EQ(engine.book().order_count(), 2u);
}

TEST(MatchingEngineTest, OpeningAuctionDeterminesPrice) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Set up PreOpen
    engine.force_state(InstrumentState::Inactive);
    ASSERT_TRUE(engine.transition_state(InstrumentState::PreOpen));

    // Buy at 10200, qty 100
    Order* buy1 = make_and_populate(pool, Side::Buy, 10200, 100);
    buy1->order_id = 1;
    engine.process_new_order(buy1);

    // Sell at 9800, qty 80
    Order* sell1 = make_and_populate(pool, Side::Sell, 9800, 80);
    sell1->order_id = 2;
    engine.process_new_order(sell1);

    EXPECT_EQ(engine.book().order_count(), 2u);

    // Calculate auction price — should find a crossing price
    auto [price, volume] = engine.calculate_auction_price();
    EXPECT_GT(price, 0);
    EXPECT_EQ(volume, 80u);  // Limited by sell side

    // Run the auction
    auto result = engine.run_opening_auction();
    EXPECT_EQ(result.action, MatchResult::Action::Filled);
    EXPECT_EQ(result.fills.size(), 1u);
    EXPECT_EQ(result.fills[0].fill_qty, 80u);

    // Engine should now be in Continuous state
    EXPECT_EQ(engine.state(), InstrumentState::Continuous);

    // Buy order should have 20 remaining
    EXPECT_EQ(engine.book().order_count(), 1u);
    EXPECT_EQ(engine.book().best_bid(), 10200);
    EXPECT_EQ(engine.book().best_bid_qty(), 20u);
}

TEST(MatchingEngineTest, OpeningAuctionMaximizesVolume) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    engine.force_state(InstrumentState::Inactive);
    ASSERT_TRUE(engine.transition_state(InstrumentState::PreOpen));

    // Multiple bid levels:
    // Buy 100 @ 10500
    Order* b1 = make_and_populate(pool, Side::Buy, 10500, 100);
    b1->order_id = 1;
    engine.process_new_order(b1);

    // Buy 50 @ 10200
    Order* b2 = make_and_populate(pool, Side::Buy, 10200, 50);
    b2->order_id = 2;
    engine.process_new_order(b2);

    // Buy 30 @ 10000
    Order* b3 = make_and_populate(pool, Side::Buy, 10000, 30);
    b3->order_id = 3;
    engine.process_new_order(b3);

    // Multiple ask levels:
    // Sell 80 @ 9800
    Order* s1 = make_and_populate(pool, Side::Sell, 9800, 80);
    s1->order_id = 4;
    engine.process_new_order(s1);

    // Sell 60 @ 10100
    Order* s2 = make_and_populate(pool, Side::Sell, 10100, 60);
    s2->order_id = 5;
    engine.process_new_order(s2);

    // Sell 40 @ 10400
    Order* s3 = make_and_populate(pool, Side::Sell, 10400, 40);
    s3->order_id = 6;
    engine.process_new_order(s3);

    EXPECT_EQ(engine.book().order_count(), 6u);

    // At price 10200:
    //   cum_bid (>= 10200) = 100 + 50 = 150
    //   cum_ask (<= 10200) = 80 + 60 = 140
    //   executable = min(150, 140) = 140
    //
    // At price 10100:
    //   cum_bid (>= 10100) = 100 + 50 = 150
    //   cum_ask (<= 10100) = 80 + 60 = 140
    //   executable = min(150, 140) = 140
    //
    // At price 10000:
    //   cum_bid (>= 10000) = 100 + 50 + 30 = 180
    //   cum_ask (<= 10000) = 80
    //   executable = min(180, 80) = 80
    //
    // Maximum volume is 140 at price 10200 or 10100

    auto [price, volume] = engine.calculate_auction_price();
    EXPECT_EQ(volume, 140u);
    // Price should be one of 9800, 10100, or 10200 (all yield 140)
    EXPECT_TRUE(price == 9800 || price == 10100 || price == 10200);

    auto result = engine.run_opening_auction();
    EXPECT_EQ(result.action, MatchResult::Action::Filled);

    // Total fills should sum to 140
    Quantity total_filled = 0;
    for (const auto& f : result.fills) {
        total_filled += f.fill_qty;
    }
    EXPECT_EQ(total_filled, 140u);

    EXPECT_EQ(engine.state(), InstrumentState::Continuous);
}

TEST(MatchingEngineTest, EmptyAuctionProducesNoTrades) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    engine.force_state(InstrumentState::Inactive);
    ASSERT_TRUE(engine.transition_state(InstrumentState::PreOpen));

    // No orders submitted
    auto result = engine.run_opening_auction();
    EXPECT_TRUE(result.fills.empty());
    EXPECT_EQ(engine.state(), InstrumentState::Continuous);
}

}  // namespace
}  // namespace engine
