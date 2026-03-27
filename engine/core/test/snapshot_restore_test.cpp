/// Snapshot restore into live partition tests.
#include "engine/snapshot.h"
#include "engine/journal_stub.h"
#include <gtest/gtest.h>

namespace engine {
namespace {

InstrumentConfig make_config(InstrumentId id = 100) {
    InstrumentConfig cfg{};
    cfg.instrument_id = id; cfg.tick_size = 1; cfg.lot_size = 1;
    cfg.min_quantity = 1; cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000; cfg.static_band_pct = 20.0;
    cfg.residual_policy = ResidualPolicy::Cancel;
    cfg.stp_mode = STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    return cfg;
}

PartitionMessage make_order(InstrumentId inst, Side side, Price price,
                             Quantity qty, ClientOrderId clord = 1) {
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::NewOrder;
    msg.timestamp = 1000; msg.client_order_id = clord;
    msg.instrument_id = inst; msg.side = side;
    msg.order_type = OrderType::Limit; msg.time_in_force = TimeInForce::GTC;
    msg.price = price; msg.quantity = qty;
    msg.participant_id = 1; msg.account_id = 1; msg.session_id = 1;
    return msg;
}

TEST(SnapshotRestoreTest, RestoreRecoversBookState) {
    // Build source partition with orders
    JournalStub j1(10000);
    Partition source(1, 10000, &j1);
    source.add_instrument(make_config(100));

    (void)source.process(make_order(100, Side::Buy, 9999, 50, 1));
    (void)source.process(make_order(100, Side::Buy, 9998, 30, 2));
    (void)source.process(make_order(100, Side::Sell, 10050, 80, 3));

    auto snap = snapshot::capture(source);
    EXPECT_EQ(snap.total_order_count(), 3u);
    EXPECT_EQ(snap.sequence, 3u);

    // Restore into a fresh partition
    JournalStub j2(10000);
    Partition target(1, 10000, &j2);
    target.add_instrument(make_config(100));

    EXPECT_TRUE(snapshot::restore(target, snap));

    // Verify recovered state
    EXPECT_EQ(target.sequence(), 3u);
    auto* eng = target.get_engine(100);
    ASSERT_NE(eng, nullptr);
    EXPECT_EQ(eng->book().order_count(), 3u);
    EXPECT_EQ(eng->book().best_bid(), 9999);
    EXPECT_EQ(eng->book().best_ask(), 10050);
}

TEST(SnapshotRestoreTest, ResumeProcessingAfterRestore) {
    JournalStub j1(10000);
    Partition source(1, 10000, &j1);
    source.add_instrument(make_config(100));

    (void)source.process(make_order(100, Side::Sell, 10050, 100, 1));
    auto snap = snapshot::capture(source);

    // Restore
    JournalStub j2(10000);
    Partition target(1, 10000, &j2);
    target.add_instrument(make_config(100));
    (void)snapshot::restore(target, snap);

    // Process a new order on the restored partition
    auto result = target.process(make_order(100, Side::Buy, 10050, 50, 2));

    // Buy at 10050 crosses restored sell at 10050.
    // Buy qty=50 fills against sell qty=100 → buy fully consumed → Filled.
    EXPECT_EQ(result.action, MatchResult::Action::Filled);

    // Sell should have 50 remaining
    EXPECT_EQ(target.get_engine(100)->book().best_ask(), 10050);
    EXPECT_EQ(target.get_engine(100)->book().order_count(), 1u);  // Sell with 50 remaining
}

TEST(SnapshotRestoreTest, MultiInstrumentRestore) {
    JournalStub j1(10000);
    Partition source(1, 10000, &j1);
    source.add_instrument(make_config(100));
    source.add_instrument(make_config(101));

    (void)source.process(make_order(100, Side::Buy, 9999, 50, 1));
    (void)source.process(make_order(101, Side::Sell, 10050, 30, 2));

    auto snap = snapshot::capture(source);

    JournalStub j2(10000);
    Partition target(1, 10000, &j2);
    target.add_instrument(make_config(100));
    target.add_instrument(make_config(101));
    (void)snapshot::restore(target, snap);

    EXPECT_EQ(target.get_engine(100)->book().best_bid(), 9999);
    EXPECT_EQ(target.get_engine(101)->book().best_ask(), 10050);
}

TEST(SnapshotRestoreTest, EmptySnapshotRestoresCleanly) {
    JournalStub j1(10000);
    Partition source(1, 10000, &j1);
    source.add_instrument(make_config(100));

    auto snap = snapshot::capture(source);
    EXPECT_EQ(snap.total_order_count(), 0u);

    JournalStub j2(10000);
    Partition target(1, 10000, &j2);
    target.add_instrument(make_config(100));
    EXPECT_TRUE(snapshot::restore(target, snap));
    EXPECT_EQ(target.get_engine(100)->book().order_count(), 0u);
}

}  // namespace
}  // namespace engine
