#include "engine/kill_switch.h"
#include "engine/journal_stub.h"
#include <gtest/gtest.h>
#include <unordered_set>

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

PartitionMessage make_order(InstrumentId inst, Side side, Price price, Quantity qty,
                             ParticipantId part_id = 1, ClientOrderId clord = 1) {
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::NewOrder;
    msg.timestamp = 1000; msg.client_order_id = clord;
    msg.instrument_id = inst; msg.side = side;
    msg.order_type = OrderType::Limit; msg.time_in_force = TimeInForce::Day;
    msg.price = price; msg.quantity = qty;
    msg.participant_id = part_id; msg.account_id = 1; msg.session_id = 1;
    return msg;
}

/// Simple block/unblock tracker for testing (replaces direct risk-service dep).
struct BlockTracker {
    std::unordered_set<ParticipantId> blocked;
    void block(ParticipantId id) { blocked.insert(id); }
    void unblock(ParticipantId id) { blocked.erase(id); }
    bool is_blocked(ParticipantId id) const { return blocked.count(id) > 0; }
};

TEST(KillSwitchTest, ActivateCancelsRestingOrders) {
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);
    part.add_instrument(make_config(100));

    BlockTracker tracker;
    KillSwitchController ks;
    ks.register_partition(&part);
    ks.set_block_callback(
        [&](ParticipantId id) { tracker.block(id); },
        [&](ParticipantId id) { tracker.unblock(id); });

    for (uint32_t i = 1; i <= 3; ++i) {
        (void)part.process(make_order(100, Side::Buy, 10000 - i, 50, 1, i));
    }
    EXPECT_EQ(part.get_engine(100)->book().order_count(), 3u);

    uint32_t cancelled = ks.activate(1);
    EXPECT_EQ(cancelled, 3u);
    EXPECT_EQ(part.get_engine(100)->book().order_count(), 0u);
    EXPECT_TRUE(ks.is_active(1));
    EXPECT_TRUE(tracker.is_blocked(1));
}

TEST(KillSwitchTest, BlocksNewOrdersAfterActivation) {
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);
    part.add_instrument(make_config(100));

    BlockTracker tracker;
    KillSwitchController ks;
    ks.register_partition(&part);
    ks.set_block_callback(
        [&](ParticipantId id) { tracker.block(id); },
        [&](ParticipantId id) { tracker.unblock(id); });

    ks.activate(1);
    EXPECT_TRUE(tracker.is_blocked(1));
}

TEST(KillSwitchTest, DeactivateUnblocks) {
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);
    part.add_instrument(make_config(100));

    BlockTracker tracker;
    KillSwitchController ks;
    ks.register_partition(&part);
    ks.set_block_callback(
        [&](ParticipantId id) { tracker.block(id); },
        [&](ParticipantId id) { tracker.unblock(id); });

    ks.activate(1);
    ks.deactivate(1);
    EXPECT_FALSE(ks.is_active(1));
    EXPECT_FALSE(tracker.is_blocked(1));
}

TEST(KillSwitchTest, OnlyCancelsTargetParticipant) {
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);
    part.add_instrument(make_config(100));

    BlockTracker tracker;
    KillSwitchController ks;
    ks.register_partition(&part);
    ks.set_block_callback(
        [&](ParticipantId id) { tracker.block(id); },
        [&](ParticipantId id) { tracker.unblock(id); });

    (void)part.process(make_order(100, Side::Buy, 9999, 50, 1, 1));
    (void)part.process(make_order(100, Side::Buy, 9998, 50, 1, 2));
    (void)part.process(make_order(100, Side::Sell, 10050, 30, 2, 3));
    EXPECT_EQ(part.get_engine(100)->book().order_count(), 3u);

    uint32_t cancelled = ks.activate(1);
    EXPECT_EQ(cancelled, 2u);
    EXPECT_EQ(part.get_engine(100)->book().order_count(), 1u);
}

TEST(KillSwitchTest, MultiplePartitions) {
    JournalStub j1(10000), j2(10000);
    Partition part1(1, 10000, &j1);
    Partition part2(2, 10000, &j2);
    part1.add_instrument(make_config(100));
    part2.add_instrument(make_config(200));

    BlockTracker tracker;
    KillSwitchController ks;
    ks.register_partition(&part1);
    ks.register_partition(&part2);
    ks.set_block_callback(
        [&](ParticipantId id) { tracker.block(id); },
        [&](ParticipantId id) { tracker.unblock(id); });

    (void)part1.process(make_order(100, Side::Buy, 9999, 50, 1, 1));
    (void)part2.process(make_order(200, Side::Sell, 10050, 30, 1, 2));

    uint32_t cancelled = ks.activate(1);
    EXPECT_EQ(cancelled, 2u);
    EXPECT_EQ(part1.get_engine(100)->book().order_count(), 0u);
    EXPECT_EQ(part2.get_engine(200)->book().order_count(), 0u);
}

}  // namespace
}  // namespace engine
