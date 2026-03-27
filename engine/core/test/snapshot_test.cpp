#include "engine/snapshot.h"
#include <gtest/gtest.h>

namespace engine {
namespace {

TEST(SnapshotTest, SerializeOrderRoundTrip) {
    // Create an order with known values
    Order original{};
    original.clear();
    original.order_id        = 12345;
    original.client_order_id = 67890;
    original.participant_id  = 42;
    original.account_id      = 7;
    original.session_id      = 3;
    original.instrument_id   = 100;
    original.side            = Side::Sell;
    original.order_type      = OrderType::Limit;
    original.time_in_force   = TimeInForce::GTC;
    original.price           = 50000;
    original.original_qty    = 1000;
    original.remaining_qty   = 750;
    original.cumulative_filled = 250;
    original.stp_group_id    = 99;
    original.flags           = FLAG_POST_ONLY;
    original.entry_sequence  = 555;
    original.entry_timestamp = 9999999;
    original.expiry_time     = 0;

    // Serialize
    SnapshotOrder so = snapshot::serialize_order(original);

    // Deserialize into a fresh order
    Order restored{};
    snapshot::deserialize_order(&restored, so);

    // Verify all fields round-trip correctly
    EXPECT_EQ(restored.order_id, 12345u);
    EXPECT_EQ(restored.client_order_id, 67890u);
    EXPECT_EQ(restored.participant_id, 42u);
    EXPECT_EQ(restored.account_id, 7u);
    EXPECT_EQ(restored.session_id, 3u);
    EXPECT_EQ(restored.instrument_id, 100u);
    EXPECT_EQ(restored.side, Side::Sell);
    EXPECT_EQ(restored.order_type, OrderType::Limit);
    EXPECT_EQ(restored.time_in_force, TimeInForce::GTC);
    EXPECT_EQ(restored.price, 50000);
    EXPECT_EQ(restored.original_qty, 1000u);
    EXPECT_EQ(restored.remaining_qty, 750u);
    EXPECT_EQ(restored.cumulative_filled, 250u);
    EXPECT_EQ(restored.stp_group_id, 99u);
    EXPECT_EQ(restored.flags, FLAG_POST_ONLY);
    EXPECT_EQ(restored.entry_sequence, 555u);
    EXPECT_EQ(restored.entry_timestamp, 9999999u);
    EXPECT_EQ(restored.expiry_time, 0u);

    // Intrusive pointers should be null after deserialization
    EXPECT_EQ(restored.prev_in_level, nullptr);
    EXPECT_EQ(restored.next_in_level, nullptr);
}

TEST(SnapshotTest, CaptureReturnsPartitionMetadata) {
    Partition part(3, 1000);

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
    part.add_instrument(cfg);

    // Process a message to advance sequence
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::NewOrder;
    msg.instrument_id = 100;
    msg.side = Side::Buy;
    msg.order_type = OrderType::Limit;
    msg.time_in_force = TimeInForce::Day;
    msg.price = 10000;
    msg.quantity = 50;
    msg.participant_id = 1;
    msg.account_id = 1;
    msg.session_id = 1;
    part.process(msg);

    Snapshot snap = snapshot::capture(part);
    EXPECT_EQ(snap.partition_id, 3);
    EXPECT_EQ(snap.sequence, 1u);  // One message processed
}

TEST(SnapshotTest, SnapshotTotalOrderCount) {
    Snapshot snap;
    snap.partition_id = 1;
    snap.sequence = 100;

    SnapshotBook book1;
    book1.instrument_id = 100;
    book1.orders.resize(5);  // 5 orders

    SnapshotBook book2;
    book2.instrument_id = 101;
    book2.orders.resize(3);  // 3 orders

    snap.books.push_back(book1);
    snap.books.push_back(book2);

    EXPECT_EQ(snap.total_order_count(), 8u);
}

}  // namespace
}  // namespace engine
