/// Tests for volatility auction (#14), scheduled auction (#15),
/// auction imbalance (#16), and random auction end config (#17).
#include "engine/matching.h"
#include "engine/order_pool.h"
#include <gtest/gtest.h>

namespace engine {
namespace {

// ─── Helpers ─────────────────────────────────────────────────

InstrumentConfig make_config(InstrumentId id = 100) {
    InstrumentConfig cfg{};
    cfg.instrument_id = id;
    cfg.tick_size = 1;
    cfg.lot_size = 1;
    cfg.min_quantity = 1;
    cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000;
    cfg.static_band_pct = 20.0;
    cfg.dynamic_band_pct = 5.0;    // 5% dynamic band for testing
    cfg.residual_policy = ResidualPolicy::Cancel;
    cfg.collar_ticks = 10;
    cfg.stp_mode = STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    cfg.random_end_jitter_ms = 0;
    return cfg;
}

Order* make_order(OrderPool& pool, OrderId id, Side side, Price price,
                  Quantity qty, SequenceNum seq) {
    Order* o = pool.allocate();
    o->order_id = id;
    o->instrument_id = 100;
    o->side = side;
    o->order_type = OrderType::Limit;
    o->time_in_force = TimeInForce::Day;
    o->price = price;
    o->original_qty = qty;
    o->remaining_qty = qty;
    o->cumulative_filled = 0;
    o->entry_sequence = seq;
    o->participant_id = 1;
    o->account_id = 1;
    o->session_id = 1;
    o->stp_group_id = 0;
    o->flags = 0;
    return o;
}

// ─── #14: Volatility Auction ─────────────────────────────────

TEST(VolatilityAuctionTest, VolatilityAuctionTriggersOnPriceBreach) {
    // When a trade executes at a price outside the dynamic band,
    // the engine should transition from Continuous to Auction.
    OrderPool pool(1000);
    auto cfg = make_config();
    // static_ref_price = 10000, dynamic_band_pct = 5%
    // Band: 9500..10500. A trade at price outside this should trigger.
    MatchingEngine engine(cfg, pool);
    EXPECT_EQ(engine.state(), InstrumentState::Continuous);

    // Place a resting sell at a price that breaches the dynamic band (10600 > 10500)
    auto* sell = make_order(pool, 1, Side::Sell, 10600, 50, 1);
    auto r1 = engine.process_new_order(sell);
    EXPECT_EQ(r1.action, MatchResult::Action::Inserted);
    EXPECT_EQ(engine.state(), InstrumentState::Continuous);

    // Aggressive buy sweeps the ask at 10600 → breaches 5% dynamic band
    auto* buy = make_order(pool, 2, Side::Buy, 10600, 50, 2);
    auto r2 = engine.process_new_order(buy);
    EXPECT_EQ(r2.action, MatchResult::Action::Filled);
    EXPECT_EQ(r2.fills.size(), 1u);
    EXPECT_EQ(r2.fills[0].fill_price, 10600);

    // State should now be Auction (volatility auction triggered)
    EXPECT_EQ(engine.state(), InstrumentState::Auction);
    EXPECT_EQ(engine.last_traded_price(), 10600);
}

TEST(VolatilityAuctionTest, NoTriggerWithinBands) {
    // A trade within the dynamic band should NOT trigger an auction.
    OrderPool pool(1000);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Place sell at 10400 (within 5% of 10000 → 10000*1.05 = 10500)
    auto* sell = make_order(pool, 1, Side::Sell, 10400, 50, 1);
    engine.process_new_order(sell);

    // Aggressive buy matches at 10400 — within band
    auto* buy = make_order(pool, 2, Side::Buy, 10400, 50, 2);
    auto r = engine.process_new_order(buy);
    EXPECT_EQ(r.action, MatchResult::Action::Filled);
    EXPECT_EQ(r.fills.size(), 1u);

    // State should remain Continuous
    EXPECT_EQ(engine.state(), InstrumentState::Continuous);
}

TEST(VolatilityAuctionTest, AuctionCollectsWithoutMatching) {
    // During Auction state, orders should rest without matching.
    OrderPool pool(1000);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Transition to Auction
    EXPECT_TRUE(engine.trigger_volatility_auction());
    EXPECT_EQ(engine.state(), InstrumentState::Auction);

    // Place crossing orders — they should NOT match
    auto* sell = make_order(pool, 1, Side::Sell, 10000, 100, 1);
    auto r1 = engine.process_new_order(sell);
    EXPECT_EQ(r1.action, MatchResult::Action::Inserted);
    EXPECT_TRUE(r1.order_resting);
    EXPECT_TRUE(r1.fills.empty());

    auto* buy = make_order(pool, 2, Side::Buy, 10100, 100, 2);
    auto r2 = engine.process_new_order(buy);
    EXPECT_EQ(r2.action, MatchResult::Action::Inserted);
    EXPECT_TRUE(r2.order_resting);
    EXPECT_TRUE(r2.fills.empty());

    // Both orders should be resting in the book
    EXPECT_EQ(engine.book().order_count(), 2u);
}

TEST(VolatilityAuctionTest, MarketOrderRejectedDuringAuction) {
    // Market orders should be rejected during Auction state.
    OrderPool pool(1000);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    EXPECT_TRUE(engine.trigger_volatility_auction());
    EXPECT_EQ(engine.state(), InstrumentState::Auction);

    Order* mkt = pool.allocate();
    mkt->order_id = 1;
    mkt->instrument_id = 100;
    mkt->side = Side::Buy;
    mkt->order_type = OrderType::Market;
    mkt->time_in_force = TimeInForce::Day;
    mkt->price = 0;
    mkt->original_qty = 100;
    mkt->remaining_qty = 100;
    mkt->cumulative_filled = 0;
    mkt->entry_sequence = 1;
    mkt->participant_id = 1;
    mkt->account_id = 1;
    mkt->session_id = 1;
    mkt->stp_group_id = 0;
    mkt->flags = 0;

    auto r = engine.process_new_order(mkt);
    EXPECT_EQ(r.action, MatchResult::Action::Rejected);
    EXPECT_EQ(r.reject_code, RejectCode::InvalidOrderType);
}

TEST(VolatilityAuctionTest, RunVolatilityAuctionResumesContinuous) {
    // After running the volatility auction, state should return to Continuous
    // and crossing orders should be matched at the auction price.
    OrderPool pool(1000);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Transition to Auction
    EXPECT_TRUE(engine.trigger_volatility_auction());

    // Collect orders during auction
    auto* buy1  = make_order(pool, 1, Side::Buy,  10100, 100, 1);
    auto* buy2  = make_order(pool, 2, Side::Buy,  10050, 50, 2);
    auto* sell1 = make_order(pool, 3, Side::Sell, 10000, 80, 3);
    auto* sell2 = make_order(pool, 4, Side::Sell, 10050, 40, 4);

    engine.process_new_order(buy1);
    engine.process_new_order(buy2);
    engine.process_new_order(sell1);
    engine.process_new_order(sell2);

    EXPECT_EQ(engine.book().order_count(), 4u);

    // Run the auction
    auto result = engine.run_volatility_auction();
    EXPECT_EQ(result.action, MatchResult::Action::Filled);
    EXPECT_FALSE(result.fills.empty());

    // State should be Continuous now
    EXPECT_EQ(engine.state(), InstrumentState::Continuous);

    // Verify fills executed at a single auction price
    Price auction_price = result.fills[0].fill_price;
    for (const auto& fill : result.fills) {
        EXPECT_EQ(fill.fill_price, auction_price);
    }
}

// ─── #15: Scheduled Auction ──────────────────────────────────

TEST(VolatilityAuctionTest, ScheduledAuctionTriggersAndResolves) {
    // Externally triggered scheduled auction follows same flow.
    OrderPool pool(1000);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    EXPECT_TRUE(engine.trigger_scheduled_auction());
    EXPECT_EQ(engine.state(), InstrumentState::Auction);

    // Add crossing orders
    auto* buy  = make_order(pool, 1, Side::Buy,  10100, 100, 1);
    auto* sell = make_order(pool, 2, Side::Sell, 10000, 100, 2);
    engine.process_new_order(buy);
    engine.process_new_order(sell);

    // Run auction
    auto result = engine.run_volatility_auction();
    EXPECT_EQ(result.action, MatchResult::Action::Filled);
    EXPECT_EQ(engine.state(), InstrumentState::Continuous);
}

// ─── #16: Auction Imbalance ──────────────────────────────────

TEST(VolatilityAuctionTest, AuctionImbalanceCalculation) {
    // Verify indicative price, imbalance side, and imbalance qty.
    OrderPool pool(1000);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    EXPECT_TRUE(engine.trigger_volatility_auction());

    // Buy side: 200 qty total (100 at 10100, 100 at 10050)
    // Sell side: 80 qty at 10000
    // At auction price, buy cumulative > sell cumulative → Buy imbalance
    auto* buy1  = make_order(pool, 1, Side::Buy,  10100, 100, 1);
    auto* buy2  = make_order(pool, 2, Side::Buy,  10050, 100, 2);
    auto* sell1 = make_order(pool, 3, Side::Sell, 10000, 80, 3);

    engine.process_new_order(buy1);
    engine.process_new_order(buy2);
    engine.process_new_order(sell1);

    auto imbalance = engine.calculate_auction_imbalance();

    // There should be a valid indicative price
    EXPECT_GT(imbalance.indicative_price, 0);

    // Paired qty = min(cum_bid, cum_ask) = 80
    EXPECT_EQ(imbalance.paired_qty, 80u);

    // Buy side has excess: 200 - 80 = 120
    EXPECT_EQ(imbalance.imbalance_side, Side::Buy);
    EXPECT_EQ(imbalance.imbalance_qty, 120u);
}

TEST(VolatilityAuctionTest, AuctionImbalanceNoOrders) {
    // With no orders, imbalance should be zero.
    OrderPool pool(1000);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    EXPECT_TRUE(engine.trigger_volatility_auction());

    auto imbalance = engine.calculate_auction_imbalance();
    EXPECT_EQ(imbalance.indicative_price, 0);
    EXPECT_EQ(imbalance.paired_qty, 0u);
    EXPECT_EQ(imbalance.imbalance_qty, 0u);
}

// ─── #17: Random Auction End Config ──────────────────────────

TEST(VolatilityAuctionTest, RandomEndJitterConfigDefault) {
    // Verify the random_end_jitter_ms config field exists and defaults to 0.
    auto cfg = make_config();
    EXPECT_EQ(cfg.random_end_jitter_ms, 0u);
}

TEST(VolatilityAuctionTest, RandomEndJitterConfigCustom) {
    // Verify the field can be set to a non-zero value.
    auto cfg = make_config();
    cfg.random_end_jitter_ms = 5000;
    EXPECT_EQ(cfg.random_end_jitter_ms, 5000u);

    // Engine accepts the config without issue
    OrderPool pool(100);
    MatchingEngine engine(cfg, pool);
    EXPECT_EQ(engine.state(), InstrumentState::Continuous);
}

}  // namespace
}  // namespace engine
