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
    return cfg;
}

Order* make_order(OrderPool& pool, Side side, Price price, Quantity qty,
                  TimeInForce tif = TimeInForce::Day) {
    Order* o = pool.allocate();
    if (!o) return nullptr;
    o->instrument_id = 100;
    o->side = side;
    o->order_type = OrderType::Limit;
    o->time_in_force = tif;
    o->price = price;
    o->original_qty = qty;
    o->remaining_qty = qty;
    o->display_qty = 0;
    o->cumulative_filled = 0;
    o->stop_price = 0;
    o->participant_id = 1;
    o->account_id = 1;
    o->session_id = 1;
    o->stp_group_id = 0;
    o->entry_sequence = 1;
    o->flags = 0;
    return o;
}

// ═══════════════════════════════════════════════════════════
// AuctionOnly TIF (#7)
// ═══════════════════════════════════════════════════════════

TEST(AuctionOnlyTest, AcceptedDuringPreOpen) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    engine.force_state(InstrumentState::PreOpen);

    Order* o = make_order(pool, Side::Buy, 10000, 100, TimeInForce::AuctionOnly);
    o->order_id = 1;
    auto result = engine.process_new_order(o);

    EXPECT_EQ(result.action, MatchResult::Action::Inserted);
    EXPECT_TRUE(result.order_resting);
}

TEST(AuctionOnlyTest, AcceptedDuringClosingAuction) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Continuous → ClosingAuction
    (void)engine.transition_state(InstrumentState::ClosingAuction);

    Order* o = make_order(pool, Side::Sell, 10000, 100, TimeInForce::AuctionOnly);
    o->order_id = 1;
    auto result = engine.process_new_order(o);

    EXPECT_EQ(result.action, MatchResult::Action::Inserted);
}

TEST(AuctionOnlyTest, RejectedDuringContinuous) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);
    // Default state is Continuous

    Order* o = make_order(pool, Side::Buy, 10000, 100, TimeInForce::AuctionOnly);
    o->order_id = 1;
    auto result = engine.process_new_order(o);

    EXPECT_EQ(result.action, MatchResult::Action::Rejected);
    EXPECT_EQ(result.reject_code, RejectCode::InvalidOrderType);
}

// ═══════════════════════════════════════════════════════════
// Dynamic Price Bands (#23)
// ═══════════════════════════════════════════════════════════

TEST(DynamicBandTest, RejectsOrderOutsideDynamicBand) {
    OrderPool pool(100);
    auto cfg = make_config();
    cfg.dynamic_band_pct = 5.0;  // 5% from last trade
    MatchingEngine engine(cfg, pool);

    // First: create a trade to establish last_traded_price_
    Order* sell = make_order(pool, Side::Sell, 10000, 100);
    sell->order_id = 1;
    engine.process_new_order(sell);

    Order* buy = make_order(pool, Side::Buy, 10000, 100);
    buy->order_id = 2;
    engine.process_new_order(buy);
    // Now last_traded_price_ = 10000

    // Order at 10600 = 6% deviation from 10000 → breaches 5% dynamic band
    Order* far = make_order(pool, Side::Sell, 10600, 50);
    far->order_id = 3;
    auto result = engine.process_new_order(far);

    EXPECT_EQ(result.action, MatchResult::Action::Rejected);
    EXPECT_EQ(result.reject_code, RejectCode::PriceOutsideBands);
}

TEST(DynamicBandTest, AcceptsOrderWithinDynamicBand) {
    OrderPool pool(100);
    auto cfg = make_config();
    cfg.dynamic_band_pct = 5.0;
    MatchingEngine engine(cfg, pool);

    // Create trade
    Order* sell = make_order(pool, Side::Sell, 10000, 100);
    sell->order_id = 1;
    engine.process_new_order(sell);

    Order* buy = make_order(pool, Side::Buy, 10000, 100);
    buy->order_id = 2;
    engine.process_new_order(buy);

    // Order at 10400 = 4% → within 5% band
    Order* ok = make_order(pool, Side::Sell, 10400, 50);
    ok->order_id = 3;
    auto result = engine.process_new_order(ok);

    EXPECT_EQ(result.action, MatchResult::Action::Inserted);
}

TEST(DynamicBandTest, NoBandCheckBeforeFirstTrade) {
    OrderPool pool(100);
    auto cfg = make_config();
    cfg.dynamic_band_pct = 5.0;
    MatchingEngine engine(cfg, pool);

    // No trades yet → dynamic band not active (last_traded_price_ = 0)
    Order* o = make_order(pool, Side::Sell, 11000, 50);
    o->order_id = 1;
    auto result = engine.process_new_order(o);

    // Should pass (only static band applies, and 11000 is within 20% of 10000)
    EXPECT_EQ(result.action, MatchResult::Action::Inserted);
}

}  // namespace
}  // namespace engine
