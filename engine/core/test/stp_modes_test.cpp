#include "engine/matching.h"
#include <gtest/gtest.h>

#include <algorithm>

namespace engine {
namespace {

InstrumentConfig make_config(STPMode stp_mode = STPMode::CancelNewest) {
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
    cfg.stp_mode = stp_mode;
    cfg.qty_increase_loses_priority = true;
    return cfg;
}

Order* make_order(OrderPool& pool, Side side, Price price, Quantity qty,
                  ParticipantId participant = 1, uint32_t stp_group = 0) {
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
    o->stp_group_id = stp_group;
    o->entry_sequence = 1;
    o->flags = 0;
    return o;
}

// ═══════════════════════════════════════════════════════════
// STP CancelNewest (existing behavior, regression test)
// ═══════════════════════════════════════════════════════════

TEST(STPModeTest, CancelNewest_CancelsAggressive) {
    OrderPool pool(100);
    auto cfg = make_config(STPMode::CancelNewest);
    MatchingEngine engine(cfg, pool);

    // Rest a sell at 10000 with STP group 42
    Order* sell = make_order(pool, Side::Sell, 10000, 100, 1, 42);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // Incoming buy from same STP group should be cancelled
    Order* buy = make_order(pool, Side::Buy, 10000, 50, 1, 42);
    buy->order_id = 2;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.fills.size(), 0u);
    // Passive sell should still be in book
    EXPECT_EQ(engine.book().order_count(), 1u);
    EXPECT_EQ(engine.book().best_ask(), 10000);
}

TEST(STPModeTest, CancelNewest_NoSTPWhenGroupZero) {
    OrderPool pool(100);
    auto cfg = make_config(STPMode::CancelNewest);
    MatchingEngine engine(cfg, pool);

    // STP group 0 means STP not active
    Order* sell = make_order(pool, Side::Sell, 10000, 100, 1, 0);
    sell->order_id = 1;
    engine.process_new_order(sell);

    Order* buy = make_order(pool, Side::Buy, 10000, 50, 1, 0);
    buy->order_id = 2;
    auto result = engine.process_new_order(buy);

    // Should match normally
    EXPECT_EQ(result.fills.size(), 1u);
    EXPECT_EQ(result.fills[0].fill_qty, 50u);
}

// ═══════════════════════════════════════════════════════════
// STP CancelOldest (#9)
// ═══════════════════════════════════════════════════════════

TEST(STPModeTest, CancelOldest_CancelsPassive) {
    OrderPool pool(100);
    auto cfg = make_config(STPMode::CancelOldest);
    MatchingEngine engine(cfg, pool);

    // Rest a sell at 10000 with STP group 42
    Order* sell = make_order(pool, Side::Sell, 10000, 100, 1, 42);
    sell->order_id = 1;
    engine.process_new_order(sell);
    EXPECT_EQ(engine.book().order_count(), 1u);

    // Incoming buy from same STP group — passive should be cancelled, incoming rests
    Order* buy = make_order(pool, Side::Buy, 10000, 50, 1, 42);
    buy->order_id = 2;
    auto result = engine.process_new_order(buy);

    // No fills — STP prevented the trade
    EXPECT_EQ(result.fills.size(), 0u);
    // Passive was cancelled
    EXPECT_TRUE(std::find(result.cancelled_ids.begin(), result.cancelled_ids.end(), 1)
                != result.cancelled_ids.end());
    // Incoming order should rest in book (it crossed but passive was removed)
    // Book should have the buy resting
    EXPECT_EQ(engine.book().order_count(), 1u);
    EXPECT_EQ(engine.book().best_bid(), 10000);
}

TEST(STPModeTest, CancelOldest_MatchesNextNonSTPOrder) {
    OrderPool pool(100);
    auto cfg = make_config(STPMode::CancelOldest);
    MatchingEngine engine(cfg, pool);

    // Rest sell from STP group 42
    Order* sell1 = make_order(pool, Side::Sell, 10000, 100, 1, 42);
    sell1->order_id = 1;
    engine.process_new_order(sell1);

    // Rest another sell from different STP group
    Order* sell2 = make_order(pool, Side::Sell, 10000, 100, 2, 0);
    sell2->order_id = 2;
    engine.process_new_order(sell2);

    // Incoming buy from STP group 42 — should skip sell1, match with sell2
    Order* buy = make_order(pool, Side::Buy, 10000, 50, 1, 42);
    buy->order_id = 3;
    auto result = engine.process_new_order(buy);

    // sell1 cancelled by STP, matched with sell2
    EXPECT_EQ(result.fills.size(), 1u);
    EXPECT_EQ(result.fills[0].fill_qty, 50u);
    EXPECT_TRUE(std::find(result.cancelled_ids.begin(), result.cancelled_ids.end(), 1)
                != result.cancelled_ids.end());
}

// ═══════════════════════════════════════════════════════════
// STP CancelBoth (#10)
// ═══════════════════════════════════════════════════════════

TEST(STPModeTest, CancelBoth_CancelsBothSides) {
    OrderPool pool(100);
    auto cfg = make_config(STPMode::CancelBoth);
    MatchingEngine engine(cfg, pool);

    Order* sell = make_order(pool, Side::Sell, 10000, 100, 1, 42);
    sell->order_id = 1;
    engine.process_new_order(sell);

    Order* buy = make_order(pool, Side::Buy, 10000, 50, 1, 42);
    buy->order_id = 2;
    auto result = engine.process_new_order(buy);

    // No fills, both cancelled
    EXPECT_EQ(result.fills.size(), 0u);
    // Passive should be in cancelled list
    EXPECT_TRUE(std::find(result.cancelled_ids.begin(), result.cancelled_ids.end(), 1)
                != result.cancelled_ids.end());
    // Book should be empty
    EXPECT_EQ(engine.book().order_count(), 0u);
}

// ═══════════════════════════════════════════════════════════
// STP Decrement (#11)
// ═══════════════════════════════════════════════════════════

TEST(STPModeTest, Decrement_ReducesIncomingQty) {
    OrderPool pool(100);
    auto cfg = make_config(STPMode::Decrement);
    MatchingEngine engine(cfg, pool);

    // Rest sell 30 with STP group 42
    Order* sell = make_order(pool, Side::Sell, 10000, 30, 1, 42);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // Rest another sell 50 from different group
    Order* sell2 = make_order(pool, Side::Sell, 10000, 50, 2, 0);
    sell2->order_id = 2;
    engine.process_new_order(sell2);

    // Buy 100 from STP group 42 — should reduce by 30, then match 70 with sell2
    Order* buy = make_order(pool, Side::Buy, 10000, 100, 1, 42);
    buy->order_id = 3;
    auto result = engine.process_new_order(buy);

    // sell1 cancelled, buy reduced to 70, matched 50 with sell2, 20 rests
    EXPECT_TRUE(std::find(result.cancelled_ids.begin(), result.cancelled_ids.end(), 1)
                != result.cancelled_ids.end());
    EXPECT_EQ(result.fills.size(), 1u);
    EXPECT_EQ(result.fills[0].fill_qty, 50u);
}

TEST(STPModeTest, Decrement_CancelsIncomingWhenSmallerThanPassive) {
    OrderPool pool(100);
    auto cfg = make_config(STPMode::Decrement);
    MatchingEngine engine(cfg, pool);

    // Rest sell 100 with STP group 42
    Order* sell = make_order(pool, Side::Sell, 10000, 100, 1, 42);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // Buy 50 — incoming <= passive, so incoming gets cancelled
    Order* buy = make_order(pool, Side::Buy, 10000, 50, 1, 42);
    buy->order_id = 2;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.fills.size(), 0u);
    // Passive sell should still be resting
    EXPECT_EQ(engine.book().order_count(), 1u);
    EXPECT_EQ(engine.book().best_ask(), 10000);
}

// ═══════════════════════════════════════════════════════════
// Post-Only (#5)
// ═══════════════════════════════════════════════════════════

TEST(PostOnlyTest, RejectsWhenWouldCross) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Rest a sell at 10000
    Order* sell = make_order(pool, Side::Sell, 10000, 100);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // Post-only buy at 10000 would cross — should be rejected
    Order* buy = make_order(pool, Side::Buy, 10000, 50);
    buy->order_id = 2;
    buy->flags = FLAG_POST_ONLY;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.action, MatchResult::Action::Rejected);
    EXPECT_EQ(result.reject_code, RejectCode::PostOnlyWouldMatch);
    EXPECT_EQ(engine.book().order_count(), 1u);  // Only the sell remains
}

TEST(PostOnlyTest, AcceptsWhenWouldNotCross) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Rest a sell at 10000
    Order* sell = make_order(pool, Side::Sell, 10000, 100);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // Post-only buy at 9999 would NOT cross — should rest
    Order* buy = make_order(pool, Side::Buy, 9999, 50);
    buy->order_id = 2;
    buy->flags = FLAG_POST_ONLY;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.action, MatchResult::Action::Inserted);
    EXPECT_TRUE(result.order_resting);
    EXPECT_EQ(engine.book().order_count(), 2u);
}

TEST(PostOnlyTest, AcceptsWhenBookEmpty) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Post-only buy on empty book — should rest
    Order* buy = make_order(pool, Side::Buy, 10000, 50);
    buy->order_id = 1;
    buy->flags = FLAG_POST_ONLY;
    auto result = engine.process_new_order(buy);

    EXPECT_EQ(result.action, MatchResult::Action::Inserted);
    EXPECT_TRUE(result.order_resting);
}

// ═══════════════════════════════════════════════════════════
// Last Traded Price (#18) & Dynamic Ref Price Update (#20)
// ═══════════════════════════════════════════════════════════

TEST(LastTradedPriceTest, TracksAfterTrade) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    EXPECT_EQ(engine.last_traded_price(), 0);

    // Rest a sell at 10050
    Order* sell = make_order(pool, Side::Sell, 10050, 100);
    sell->order_id = 1;
    engine.process_new_order(sell);

    // Match with a buy
    Order* buy = make_order(pool, Side::Buy, 10050, 50);
    buy->order_id = 2;
    engine.process_new_order(buy);

    EXPECT_EQ(engine.last_traded_price(), 10050);
}

TEST(LastTradedPriceTest, UpdatesOnEachTrade) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Rest sells at different prices
    Order* sell1 = make_order(pool, Side::Sell, 10050, 100);
    sell1->order_id = 1;
    engine.process_new_order(sell1);

    Order* sell2 = make_order(pool, Side::Sell, 10100, 100);
    sell2->order_id = 2;
    engine.process_new_order(sell2);

    // Buy sweeps both levels
    Order* buy = make_order(pool, Side::Buy, 10100, 150);
    buy->order_id = 3;
    auto result = engine.process_new_order(buy);

    // Last traded price should be 10100 (the last fill)
    EXPECT_EQ(engine.last_traded_price(), 10100);
    EXPECT_EQ(result.fills.size(), 2u);
}

}  // namespace
}  // namespace engine
