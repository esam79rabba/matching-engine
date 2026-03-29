/// Unit tests for OrderSessionTracker.
///
/// Validates the order-to-session mapping used to route fill
/// notifications to passive participants.

#include "order_session_tracker.h"

#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace {

using namespace engine;

// ─── Basic tracking ──────────────────────────────────────────

TEST(OrderSessionTracker, TrackAndFind) {
    OrderSessionTracker tracker;

    OrderSessionInfo info;
    info.session_id = 1;
    info.side = Side::Sell;
    info.instrument_id = 100;
    info.original_qty = 500;
    info.raw_client_order_id = "ORD001";

    tracker.track(1001, info);

    auto* found = tracker.find(1001);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->session_id, 1u);
    EXPECT_EQ(found->side, Side::Sell);
    EXPECT_EQ(found->instrument_id, 100u);
    EXPECT_EQ(found->original_qty, 500u);
    EXPECT_EQ(found->cumulative_filled, 0u);
    EXPECT_EQ(found->raw_client_order_id, "ORD001");
    EXPECT_EQ(tracker.count(), 1u);
}

TEST(OrderSessionTracker, FindReturnsNullForUnknown) {
    OrderSessionTracker tracker;
    EXPECT_EQ(tracker.find(9999), nullptr);
}

TEST(OrderSessionTracker, UntrackRemoves) {
    OrderSessionTracker tracker;

    OrderSessionInfo info;
    info.session_id = 2;
    info.side = Side::Buy;
    info.instrument_id = 200;
    info.original_qty = 100;

    tracker.track(2001, info);
    EXPECT_NE(tracker.find(2001), nullptr);
    EXPECT_EQ(tracker.count(), 1u);

    tracker.untrack(2001);
    EXPECT_EQ(tracker.find(2001), nullptr);
    EXPECT_EQ(tracker.count(), 0u);
}

TEST(OrderSessionTracker, UntrackNonexistentIsNoOp) {
    OrderSessionTracker tracker;
    tracker.untrack(9999);  // Should not crash
    EXPECT_EQ(tracker.count(), 0u);
}

// ─── Fill updates ────────────────────────────────────────────

TEST(OrderSessionTracker, UpdateFill) {
    OrderSessionTracker tracker;

    OrderSessionInfo info;
    info.session_id = 1;
    info.side = Side::Sell;
    info.instrument_id = 100;
    info.original_qty = 1000;

    tracker.track(3001, info);

    auto new_cum = tracker.update_fill(3001, 300);
    EXPECT_EQ(new_cum, 300u);

    new_cum = tracker.update_fill(3001, 200);
    EXPECT_EQ(new_cum, 500u);

    auto* found = tracker.find(3001);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->cumulative_filled, 500u);
}

TEST(OrderSessionTracker, UpdateFillReturnsZeroForUnknown) {
    OrderSessionTracker tracker;
    auto result = tracker.update_fill(9999, 100);
    EXPECT_EQ(result, 0u);
}

// ─── Snapshot and fill (atomic operation) ────────────────────

TEST(OrderSessionTracker, SnapshotAndFill) {
    OrderSessionTracker tracker;

    OrderSessionInfo info;
    info.session_id = 3;
    info.side = Side::Sell;
    info.instrument_id = 100;
    info.original_qty = 500;
    info.raw_client_order_id = "PASS001";

    tracker.track(4001, info);

    OrderSessionInfo out;
    bool ok = tracker.snapshot_and_fill(4001, 200, out);
    ASSERT_TRUE(ok);
    EXPECT_EQ(out.session_id, 3u);
    EXPECT_EQ(out.side, Side::Sell);
    EXPECT_EQ(out.cumulative_filled, 200u);
    EXPECT_EQ(out.original_qty, 500u);
    EXPECT_EQ(out.raw_client_order_id, "PASS001");

    // Second fill
    ok = tracker.snapshot_and_fill(4001, 300, out);
    ASSERT_TRUE(ok);
    EXPECT_EQ(out.cumulative_filled, 500u);
}

TEST(OrderSessionTracker, SnapshotAndFillReturnsFalseForUnknown) {
    OrderSessionTracker tracker;
    OrderSessionInfo out;
    EXPECT_FALSE(tracker.snapshot_and_fill(9999, 100, out));
}

// ─── Multiple orders ─────────────────────────────────────────

TEST(OrderSessionTracker, MultipleOrders) {
    OrderSessionTracker tracker;

    OrderSessionInfo info1;
    info1.session_id = 1;
    info1.side = Side::Buy;
    info1.instrument_id = 100;
    info1.original_qty = 100;

    OrderSessionInfo info2;
    info2.session_id = 2;
    info2.side = Side::Sell;
    info2.instrument_id = 100;
    info2.original_qty = 200;

    OrderSessionInfo info3;
    info3.session_id = 1;
    info3.side = Side::Sell;
    info3.instrument_id = 200;
    info3.original_qty = 300;

    tracker.track(5001, info1);
    tracker.track(5002, info2);
    tracker.track(5003, info3);

    EXPECT_EQ(tracker.count(), 3u);

    auto* f1 = tracker.find(5001);
    auto* f2 = tracker.find(5002);
    auto* f3 = tracker.find(5003);
    ASSERT_NE(f1, nullptr);
    ASSERT_NE(f2, nullptr);
    ASSERT_NE(f3, nullptr);

    EXPECT_EQ(f1->session_id, 1u);
    EXPECT_EQ(f2->session_id, 2u);
    EXPECT_EQ(f3->session_id, 1u);
    EXPECT_EQ(f1->side, Side::Buy);
    EXPECT_EQ(f2->side, Side::Sell);
    EXPECT_EQ(f3->instrument_id, 200u);
}

// ─── Full-fill cleanup pattern ───────────────────────────────

TEST(OrderSessionTracker, UntrackOnFullFill) {
    OrderSessionTracker tracker;

    OrderSessionInfo info;
    info.session_id = 4;
    info.side = Side::Sell;
    info.instrument_id = 100;
    info.original_qty = 500;

    tracker.track(6001, info);

    // Simulate partial fill
    OrderSessionInfo out;
    tracker.snapshot_and_fill(6001, 200, out);
    EXPECT_EQ(out.cumulative_filled, 200u);
    Quantity leaves = out.original_qty - out.cumulative_filled;
    EXPECT_EQ(leaves, 300u);
    // Still tracked
    EXPECT_NE(tracker.find(6001), nullptr);

    // Simulate remaining fill
    tracker.snapshot_and_fill(6001, 300, out);
    EXPECT_EQ(out.cumulative_filled, 500u);
    leaves = out.original_qty - out.cumulative_filled;
    EXPECT_EQ(leaves, 0u);

    // Caller would untrack when leaves == 0
    tracker.untrack(6001);
    EXPECT_EQ(tracker.find(6001), nullptr);
    EXPECT_EQ(tracker.count(), 0u);
}

// ─── Overwrite on re-track ───────────────────────────────────

TEST(OrderSessionTracker, RetrackOverwrites) {
    OrderSessionTracker tracker;

    OrderSessionInfo info1;
    info1.session_id = 1;
    info1.side = Side::Buy;
    info1.instrument_id = 100;
    info1.original_qty = 100;
    tracker.track(7001, info1);

    // Overwrite with different session
    OrderSessionInfo info2;
    info2.session_id = 2;
    info2.side = Side::Sell;
    info2.instrument_id = 200;
    info2.original_qty = 500;
    tracker.track(7001, info2);

    auto* found = tracker.find(7001);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->session_id, 2u);
    EXPECT_EQ(found->side, Side::Sell);
    EXPECT_EQ(found->original_qty, 500u);
    EXPECT_EQ(tracker.count(), 1u);
}

}  // namespace
