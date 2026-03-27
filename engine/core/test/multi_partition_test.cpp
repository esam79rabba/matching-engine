/// Multi-partition integration test — verifies independent operation.
#include "engine/partition.h"
#include "engine/journal_stub.h"
#include "engine/snapshot.h"
#include <gtest/gtest.h>

namespace engine {
namespace {

InstrumentConfig make_config(InstrumentId id) {
    InstrumentConfig cfg{};
    cfg.instrument_id = id; cfg.tick_size = 1; cfg.lot_size = 1;
    cfg.min_quantity = 1; cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000; cfg.static_band_pct = 20.0;
    cfg.dynamic_band_pct = 10.0; cfg.residual_policy = ResidualPolicy::Cancel;
    cfg.collar_ticks = 10; cfg.stp_mode = STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    return cfg;
}

PartitionMessage make_order(InstrumentId inst, Side side, Price price, Quantity qty, ClientOrderId clord = 1) {
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::NewOrder;
    msg.timestamp = 1000; msg.client_order_id = clord;
    msg.instrument_id = inst; msg.side = side;
    msg.order_type = OrderType::Limit; msg.time_in_force = TimeInForce::Day;
    msg.price = price; msg.quantity = qty;
    msg.participant_id = 1; msg.account_id = 1; msg.session_id = 1;
    return msg;
}

TEST(MultiPartitionTest, IndependentJournals) {
    JournalStub journal1(10000), journal2(10000);
    Partition part1(1, 5000, &journal1);
    Partition part2(2, 5000, &journal2);
    part1.add_instrument(make_config(100));
    part2.add_instrument(make_config(200));

    // Process on partition 1
    part1.process(make_order(100, Side::Buy, 10000, 50, 1));
    part1.process(make_order(100, Side::Sell, 10050, 30, 2));

    // Process on partition 2
    part2.process(make_order(200, Side::Sell, 9000, 100, 3));

    // Verify independent journals
    EXPECT_EQ(journal1.entry_count(), 4u);  // 2 messages × (input + output)
    EXPECT_EQ(journal2.entry_count(), 2u);  // 1 message × (input + output)
}

TEST(MultiPartitionTest, IndependentSequences) {
    JournalStub j1(10000), j2(10000);
    Partition part1(1, 5000, &j1);
    Partition part2(2, 5000, &j2);
    part1.add_instrument(make_config(100));
    part2.add_instrument(make_config(200));

    part1.process(make_order(100, Side::Buy, 10000, 50));
    part1.process(make_order(100, Side::Buy, 10000, 50));
    part2.process(make_order(200, Side::Sell, 10000, 50));

    // Each partition has its own sequence counter
    EXPECT_EQ(part1.sequence(), 2u);
    EXPECT_EQ(part2.sequence(), 1u);
}

TEST(MultiPartitionTest, NoStateContamination) {
    JournalStub j1(10000), j2(10000);
    Partition part1(1, 5000, &j1);
    Partition part2(2, 5000, &j2);
    part1.add_instrument(make_config(100));
    part2.add_instrument(make_config(200));

    // Insert resting orders on each partition
    part1.process(make_order(100, Side::Buy, 10000, 100, 1));
    part2.process(make_order(200, Side::Sell, 10000, 200, 2));

    // Verify: partition 1 has no sell, partition 2 has no buy
    EXPECT_EQ(part1.get_engine(100)->book().best_bid(), 10000);
    EXPECT_EQ(part1.get_engine(100)->book().best_ask(), 0);
    EXPECT_EQ(part2.get_engine(200)->book().best_ask(), 10000);
    EXPECT_EQ(part2.get_engine(200)->book().best_bid(), 0);

    // Cross-partition instrument does not exist
    EXPECT_EQ(part1.get_engine(200), nullptr);
    EXPECT_EQ(part2.get_engine(100), nullptr);
}

TEST(MultiPartitionTest, IndependentSnapshots) {
    JournalStub j1(10000), j2(10000);
    Partition part1(1, 5000, &j1);
    Partition part2(2, 5000, &j2);
    part1.add_instrument(make_config(100));
    part2.add_instrument(make_config(200));

    part1.process(make_order(100, Side::Buy, 10000, 50, 1));
    part1.process(make_order(100, Side::Sell, 10050, 30, 2));
    part2.process(make_order(200, Side::Buy, 9500, 100, 3));

    auto snap1 = snapshot::capture(part1);
    auto snap2 = snapshot::capture(part2);

    EXPECT_EQ(snap1.partition_id, 1);
    EXPECT_EQ(snap2.partition_id, 2);
    EXPECT_EQ(snap1.sequence, 2u);
    EXPECT_EQ(snap2.sequence, 1u);
    EXPECT_EQ(snap1.total_order_count(), 2u);
    EXPECT_EQ(snap2.total_order_count(), 1u);
}

TEST(MultiPartitionTest, WrongPartitionCancelRejected) {
    JournalStub j1(10000), j2(10000);
    Partition part1(1, 5000, &j1);
    Partition part2(2, 5000, &j2);
    part1.add_instrument(make_config(100));
    part2.add_instrument(make_config(200));

    // Insert order on partition 1
    part1.process(make_order(100, Side::Buy, 10000, 50, 1));
    OrderId order_on_part1 = (static_cast<uint64_t>(1) << 48) | 1;

    // Try to cancel it on partition 2 — should be rejected
    PartitionMessage cancel{};
    cancel.type = PartitionMessage::Type::Cancel;
    cancel.timestamp = 2000;
    cancel.orig_order_id = order_on_part1;
    cancel.instrument_id = 100;

    auto result = part2.process(cancel);
    EXPECT_EQ(result.action, MatchResult::Action::Rejected);

    // Original order still resting on partition 1
    EXPECT_EQ(part1.get_engine(100)->book().order_count(), 1u);
}

TEST(MultiPartitionTest, MatchIsolatedWithinPartition) {
    JournalStub j1(10000), j2(10000);
    Partition part1(1, 5000, &j1);
    Partition part2(2, 5000, &j2);
    part1.add_instrument(make_config(100));
    part2.add_instrument(make_config(200));

    // Resting sell on partition 1
    part1.process(make_order(100, Side::Sell, 10000, 50, 1));

    // Aggressive buy on partition 1 — should match
    auto result = part1.process(make_order(100, Side::Buy, 10000, 50, 2));
    EXPECT_EQ(result.action, MatchResult::Action::Filled);

    // Partition 2 unaffected
    EXPECT_EQ(part2.get_engine(200)->book().order_count(), 0u);
}

}  // namespace
}  // namespace engine
