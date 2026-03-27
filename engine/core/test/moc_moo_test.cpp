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
    cfg.dynamic_band_pct = 0.0;
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
    o->stop_price = 0;
    o->trail_offset = 0;
    return o;
}

static uint64_t g_oid = 1;

// ─── Test 1: MOCQueuedDuringContinuous ───────────────────────
// MOC orders are accepted during continuous trading but not matched immediately.

TEST(MocMooTest, MOCQueuedDuringContinuous) {
    g_oid = 1;
    OrderPool pool(200);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);
    // Engine starts in Continuous by default

    // Place a sell in the book at 10000
    Order* sell = make_order(pool, Side::Sell, 10000, 100);
    sell->order_id = g_oid++;
    engine.process_new_order(sell);

    // Submit a MOC buy order — should be queued, NOT matched
    Order* moc_buy = make_order(pool, Side::Buy, 0, 50, OrderType::Market, TimeInForce::MarketOnClose);
    moc_buy->order_id = g_oid++;
    auto r = engine.process_new_order(moc_buy);

    EXPECT_EQ(r.action, MatchResult::Action::Inserted);
    EXPECT_TRUE(r.fills.empty());  // No fills during continuous
    EXPECT_EQ(engine.moc_queue().size(), 1u);
}

// ─── Test 2: MOCExecutesAtClosingAuction ─────────────────────
// MOC orders participate in the closing auction and fill at the auction price.

TEST(MocMooTest, MOCExecutesAtClosingAuction) {
    g_oid = 1;
    OrderPool pool(200);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);
    // Start in Continuous

    // Place a sell in the book at 10000
    Order* sell = make_order(pool, Side::Sell, 10000, 100);
    sell->order_id = g_oid++;
    engine.process_new_order(sell);

    // Submit MOC buy order
    Order* moc_buy = make_order(pool, Side::Buy, 0, 50, OrderType::Market, TimeInForce::MarketOnClose);
    moc_buy->order_id = g_oid++;
    engine.process_new_order(moc_buy);
    EXPECT_EQ(engine.moc_queue().size(), 1u);

    // Transition to ClosingAuction
    EXPECT_TRUE(engine.transition_state(InstrumentState::ClosingAuction));

    // Run closing auction — MOC orders should be moved into book and matched
    auto result = engine.run_closing_auction();

    EXPECT_FALSE(result.fills.empty());
    EXPECT_EQ(result.fills[0].fill_price, 10000);
    EXPECT_EQ(result.fills[0].fill_qty, 50u);
    EXPECT_EQ(engine.moc_queue().size(), 0u);
}

// ─── Test 3: MOORejectedAfterOpen ────────────────────────────
// MOO orders are rejected during continuous trading (too late).

TEST(MocMooTest, MOORejectedAfterOpen) {
    g_oid = 1;
    OrderPool pool(200);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);
    // Engine starts in Continuous

    // Submit MOO order during Continuous — should be rejected
    Order* moo_buy = make_order(pool, Side::Buy, 0, 50, OrderType::Market, TimeInForce::MarketOnOpen);
    moo_buy->order_id = g_oid++;
    auto r = engine.process_new_order(moo_buy);

    EXPECT_EQ(r.action, MatchResult::Action::Rejected);
    EXPECT_EQ(r.reject_code, RejectCode::MOOTooLate);
}

// ─── Test 4: MOOExecutesAtOpeningAuction ─────────────────────
// MOO orders accepted during PreOpen and fill at the opening auction price.

TEST(MocMooTest, MOOExecutesAtOpeningAuction) {
    g_oid = 1;
    OrderPool pool(200);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Transition to PreOpen
    engine.force_state(InstrumentState::PreOpen);

    // Place a sell limit order at 10000 during PreOpen (collected, not matched)
    Order* sell = make_order(pool, Side::Sell, 10000, 100);
    sell->order_id = g_oid++;
    engine.process_new_order(sell);

    // Place a buy limit order at 10000 during PreOpen
    Order* buy = make_order(pool, Side::Buy, 10000, 30);
    buy->order_id = g_oid++;
    engine.process_new_order(buy);

    // Submit MOO buy order during PreOpen — should be accepted
    Order* moo_buy = make_order(pool, Side::Buy, 0, 50, OrderType::Market, TimeInForce::MarketOnOpen);
    moo_buy->order_id = g_oid++;
    auto r = engine.process_new_order(moo_buy);
    EXPECT_EQ(r.action, MatchResult::Action::Inserted);

    // Run opening auction — MOO order should participate and fill
    auto result = engine.run_opening_auction();

    EXPECT_FALSE(result.fills.empty());
    // The auction price should be 10000 (where supply meets demand)
    EXPECT_EQ(result.fills[0].fill_price, 10000);

    // Total buy interest: 30 (limit) + 50 (MOO at INT64_MAX) = 80
    // Total sell interest at 10000: 100
    // Executable = min(80, 100) = 80
    Quantity total_filled = 0;
    for (const auto& f : result.fills) {
        total_filled += f.fill_qty;
    }
    EXPECT_EQ(total_filled, 80u);
}

}  // namespace
}  // namespace engine
