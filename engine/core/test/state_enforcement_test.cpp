/// Tests that prove state enforcement is wired into the matching engine.
#include "engine/partition.h"
#include "engine/journal_stub.h"
#include "engine/cancel_on_disconnect.h"
#include <gtest/gtest.h>

namespace engine {
namespace {

InstrumentConfig make_config(InstrumentId id = 100) {
    InstrumentConfig cfg{};
    cfg.instrument_id = id; cfg.tick_size = 1; cfg.lot_size = 1;
    cfg.min_quantity = 1; cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000; cfg.static_band_pct = 20.0;
    cfg.dynamic_band_pct = 10.0; cfg.residual_policy = ResidualPolicy::Cancel;
    cfg.collar_ticks = 10; cfg.stp_mode = STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    return cfg;
}

PartitionMessage make_order(Side side, Price price, Quantity qty, ClientOrderId clord = 1) {
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::NewOrder;
    msg.timestamp = 1000; msg.client_order_id = clord;
    msg.instrument_id = 100; msg.side = side;
    msg.order_type = OrderType::Limit; msg.time_in_force = TimeInForce::Day;
    msg.price = price; msg.quantity = qty;
    msg.participant_id = 1; msg.account_id = 1; msg.session_id = 1;
    return msg;
}

// ── State enforcement via MatchingEngine directly ───────────

TEST(StateEnforcementTest, HaltedEngineRejectsNewOrders) {
    OrderPool pool(1000);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Engine starts in Continuous (default)
    EXPECT_EQ(engine.state(), InstrumentState::Continuous);

    // Halt the instrument
    EXPECT_TRUE(engine.transition_state(InstrumentState::Halted));
    EXPECT_EQ(engine.state(), InstrumentState::Halted);

    // New order should be rejected
    Order* order = pool.allocate();
    order->order_id = 1; order->instrument_id = 100;
    order->side = Side::Buy; order->order_type = OrderType::Limit;
    order->price = 10000; order->original_qty = 100;
    order->remaining_qty = 100; order->entry_sequence = 1;

    auto result = engine.process_new_order(order);
    EXPECT_EQ(result.action, MatchResult::Action::Rejected);
    EXPECT_EQ(result.reject_code, RejectCode::InstrumentHalted);
}

TEST(StateEnforcementTest, HaltedEngineAllowsCancels) {
    OrderPool pool(1000);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    // Insert an order while continuous
    Order* order = pool.allocate();
    order->order_id = 1; order->instrument_id = 100;
    order->side = Side::Buy; order->order_type = OrderType::Limit;
    order->price = 10000; order->original_qty = 100;
    order->remaining_qty = 100; order->entry_sequence = 1;
    engine.process_new_order(order);

    // Halt
    (void)engine.transition_state(InstrumentState::Halted);

    // Cancel should still work
    auto result = engine.process_cancel(1);
    EXPECT_EQ(result.action, MatchResult::Action::Cancelled);
}

TEST(StateEnforcementTest, ResumeAllowsNewOrders) {
    OrderPool pool(1000);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    (void)engine.transition_state(InstrumentState::Halted);
    (void)engine.transition_state(InstrumentState::Continuous);

    Order* order = pool.allocate();
    order->order_id = 2; order->instrument_id = 100;
    order->side = Side::Sell; order->order_type = OrderType::Limit;
    order->price = 10050; order->original_qty = 50;
    order->remaining_qty = 50; order->entry_sequence = 2;

    auto result = engine.process_new_order(order);
    EXPECT_EQ(result.action, MatchResult::Action::Inserted);
}

TEST(StateEnforcementTest, ClosedRejectsEverything) {
    OrderPool pool(1000);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    (void)engine.transition_state(InstrumentState::Closed);

    Order* order = pool.allocate();
    order->order_id = 1; order->instrument_id = 100;
    order->side = Side::Buy; order->order_type = OrderType::Limit;
    order->price = 10000; order->original_qty = 100;
    order->remaining_qty = 100;

    auto result = engine.process_new_order(order);
    EXPECT_EQ(result.action, MatchResult::Action::Rejected);
    EXPECT_EQ(result.reject_code, RejectCode::InstrumentHalted);
}

// ── Ops halt/resume wired through partition ─────────────────

TEST(StateEnforcementTest, OpsHaltRejectsViaPartition) {
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);
    part.add_instrument(make_config());

    // Insert an order (succeeds — continuous)
    auto r1 = part.process(make_order(Side::Buy, 10000, 100, 1));
    EXPECT_EQ(r1.action, MatchResult::Action::Inserted);

    // Halt via engine
    auto* eng = part.get_engine_mut(100);
    ASSERT_NE(eng, nullptr);
    EXPECT_TRUE(eng->transition_state(InstrumentState::Halted));

    // New order rejected
    auto r2 = part.process(make_order(Side::Sell, 10000, 50, 2));
    EXPECT_EQ(r2.action, MatchResult::Action::Rejected);
    EXPECT_EQ(r2.reject_code, RejectCode::InstrumentHalted);
}

// ── Full CoD flow through partition ─────────────────────────

TEST(StateEnforcementTest, CoDFlowThroughPartition) {
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);
    part.add_instrument(make_config());

    CancelOnDisconnect cod;
    cod.configure_session(1, {.enabled = true});

    // Insert 3 orders for session 1
    for (uint32_t i = 1; i <= 3; ++i) {
        auto msg = make_order(Side::Buy, 10000 - i, 50, i);
        msg.session_id = 1;
        auto result = part.process(msg);
        EXPECT_EQ(result.action, MatchResult::Action::Inserted);

        // Track in CoD controller
        OrderId oid = (static_cast<uint64_t>(1) << 48) | i;
        cod.track_order(1, oid);
    }

    EXPECT_EQ(part.get_engine(100)->book().order_count(), 3u);

    // Session 1 disconnects
    auto orders_to_cancel = cod.on_disconnect(1);
    EXPECT_EQ(orders_to_cancel.size(), 3u);

    // Feed synthetic cancels through partition
    for (OrderId oid : orders_to_cancel) {
        PartitionMessage cancel{};
        cancel.type = PartitionMessage::Type::Cancel;
        cancel.timestamp = 5000;
        cancel.orig_order_id = oid;
        cancel.instrument_id = 100;
        auto result = part.process(cancel);
        EXPECT_EQ(result.action, MatchResult::Action::Cancelled);
    }

    // All orders cancelled
    EXPECT_EQ(part.get_engine(100)->book().order_count(), 0u);

    // Journal has records for all operations
    // 3 inserts × 2 (in+out) + 3 cancels × 2 (in+out) = 12
    EXPECT_EQ(journal.entry_count(), 12u);
}

}  // namespace
}  // namespace engine
