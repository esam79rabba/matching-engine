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

}  // namespace
}  // namespace engine
