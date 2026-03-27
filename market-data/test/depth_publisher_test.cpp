#include "marketdata/depth_publisher.h"
#include "engine/order_book.h"
#include <gtest/gtest.h>

namespace marketdata {
namespace {

// Helper: create and insert a limit order into the book.
// Returns the Order pointer (caller must keep it alive while it rests).
static engine::Order make_order(engine::OrderId id, engine::InstrumentId inst,
                                 engine::Side side, engine::Price price,
                                 engine::Quantity qty) {
    engine::Order o{};
    o.order_id = id;
    o.instrument_id = inst;
    o.side = side;
    o.order_type = engine::OrderType::Limit;
    o.time_in_force = engine::TimeInForce::GTC;
    o.price = price;
    o.original_qty = qty;
    o.remaining_qty = qty;
    o.entry_sequence = id;  // Use id as sequence for deterministic ordering
    return o;
}

TEST(DepthPublisherTest, BuildSnapshotFromBook) {
    engine::OrderBook book(1);

    // 3 bid levels, 2 ask levels
    engine::Order b1 = make_order(1, 1, engine::Side::Buy, 100, 10);
    engine::Order b2 = make_order(2, 1, engine::Side::Buy, 100, 20);  // Same level as b1
    engine::Order b3 = make_order(3, 1, engine::Side::Buy, 99, 15);
    engine::Order b4 = make_order(4, 1, engine::Side::Buy, 98, 5);
    engine::Order a1 = make_order(5, 1, engine::Side::Sell, 101, 12);
    engine::Order a2 = make_order(6, 1, engine::Side::Sell, 102, 8);

    book.insert_without_matching(&b1);
    book.insert_without_matching(&b2);
    book.insert_without_matching(&b3);
    book.insert_without_matching(&b4);
    book.insert_without_matching(&a1);
    book.insert_without_matching(&a2);

    DepthPublisher pub;
    DepthSnapshot snap = pub.build_snapshot(book);

    // Bids: 100 (qty 30, count 2), 99 (qty 15, count 1), 98 (qty 5, count 1)
    ASSERT_EQ(snap.bids.size(), 3u);
    EXPECT_EQ(snap.bids[0].price, 100);
    EXPECT_EQ(snap.bids[0].quantity, 30u);
    EXPECT_EQ(snap.bids[0].order_count, 2u);
    EXPECT_EQ(snap.bids[1].price, 99);
    EXPECT_EQ(snap.bids[1].quantity, 15u);
    EXPECT_EQ(snap.bids[2].price, 98);
    EXPECT_EQ(snap.bids[2].quantity, 5u);

    // Asks: 101 (qty 12, count 1), 102 (qty 8, count 1)
    ASSERT_EQ(snap.asks.size(), 2u);
    EXPECT_EQ(snap.asks[0].price, 101);
    EXPECT_EQ(snap.asks[0].quantity, 12u);
    EXPECT_EQ(snap.asks[1].price, 102);
    EXPECT_EQ(snap.asks[1].quantity, 8u);

    EXPECT_EQ(snap.instrument_id, 1u);
}

TEST(DepthPublisherTest, IncrementalUpdates) {
    DepthPublisher pub;

    pub.on_depth_change(1, engine::Side::Buy, 100, 50, 3);
    pub.on_depth_change(1, engine::Side::Sell, 101, 20, 1);
    pub.on_depth_change(2, engine::Side::Buy, 200, 10, 1);

    // Get updates for instrument 1
    auto updates1 = pub.get_pending_updates(1);
    ASSERT_EQ(updates1.size(), 2u);
    EXPECT_EQ(updates1[0].price, 100);
    EXPECT_EQ(updates1[0].quantity, 50u);
    EXPECT_EQ(updates1[0].order_count, 3u);
    EXPECT_EQ(updates1[1].price, 101);
    EXPECT_EQ(updates1[1].quantity, 20u);

    // Second call returns empty (pending cleared)
    auto updates1_again = pub.get_pending_updates(1);
    EXPECT_TRUE(updates1_again.empty());

    // Instrument 2 still has its update
    auto updates2 = pub.get_pending_updates(2);
    ASSERT_EQ(updates2.size(), 1u);
    EXPECT_EQ(updates2[0].price, 200);
}

TEST(DepthPublisherTest, MaxLevelsRespected) {
    engine::OrderBook book(1);

    // Insert 5 bid levels
    engine::Order orders[5];
    for (int i = 0; i < 5; ++i) {
        orders[i] = make_order(static_cast<engine::OrderId>(i + 1), 1,
                               engine::Side::Buy,
                               static_cast<engine::Price>(100 - i), 10);
        book.insert_without_matching(&orders[i]);
    }

    DepthPublisher pub;

    // Request only 3 levels
    DepthSnapshot snap = pub.build_snapshot(book, 3);
    ASSERT_EQ(snap.bids.size(), 3u);
    // Best first: 100, 99, 98
    EXPECT_EQ(snap.bids[0].price, 100);
    EXPECT_EQ(snap.bids[1].price, 99);
    EXPECT_EQ(snap.bids[2].price, 98);
}

TEST(DepthPublisherTest, EmptyBookEmptySnapshot) {
    engine::OrderBook book(42);
    DepthPublisher pub;

    DepthSnapshot snap = pub.build_snapshot(book);
    EXPECT_TRUE(snap.bids.empty());
    EXPECT_TRUE(snap.asks.empty());
    EXPECT_EQ(snap.instrument_id, 42u);
}

}  // namespace
}  // namespace marketdata
