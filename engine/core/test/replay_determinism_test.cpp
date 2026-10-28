#include "engine/partition.h"
#include "engine/journal_stub.h"
#include "engine/snapshot.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <vector>
#include <cstring>

namespace engine {
namespace {

// ─── Helpers ────────────────────────────────────────────────

InstrumentConfig make_config(InstrumentId id = 100) {
    InstrumentConfig cfg{};
    cfg.instrument_id = id;
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

PartitionMessage make_new_order(InstrumentId inst, Side side, Price price,
                                 Quantity qty, ClientOrderId clord = 1) {
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::NewOrder;
    msg.timestamp = 1000000000;
    msg.client_order_id = clord;
    msg.instrument_id = inst;
    msg.side = side;
    msg.order_type = OrderType::Limit;
    msg.time_in_force = TimeInForce::Day;
    msg.price = price;
    msg.quantity = qty;
    msg.participant_id = 1;
    msg.account_id = 1;
    msg.session_id = 1;
    msg.stp_group_id = 0;
    msg.flags = 0;
    return msg;
}

PartitionMessage make_cancel(OrderId orig) {
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::Cancel;
    msg.timestamp = 2000000000;
    msg.orig_order_id = orig;
    msg.instrument_id = 100;
    return msg;
}

/// Compare two snapshots for equality (order-level).
bool snapshots_equal(const Snapshot& a, const Snapshot& b) {
    if (a.partition_id != b.partition_id) return false;
    if (a.sequence != b.sequence) return false;
    if (a.books.size() != b.books.size()) return false;

    // Sort books by instrument_id for stable comparison
    auto sorted_a = a.books;
    auto sorted_b = b.books;
    auto cmp = [](const SnapshotBook& x, const SnapshotBook& y) {
        return x.instrument_id < y.instrument_id;
    };
    std::sort(sorted_a.begin(), sorted_a.end(), cmp);
    std::sort(sorted_b.begin(), sorted_b.end(), cmp);

    for (size_t i = 0; i < sorted_a.size(); ++i) {
        const auto& ba = sorted_a[i];
        const auto& bb = sorted_b[i];
        if (ba.instrument_id != bb.instrument_id) return false;
        if (ba.orders.size() != bb.orders.size()) return false;

        for (size_t j = 0; j < ba.orders.size(); ++j) {
            const auto& oa = ba.orders[j];
            const auto& ob = bb.orders[j];
            if (oa.order_id != ob.order_id) return false;
            if (oa.side != ob.side) return false;
            if (oa.price != ob.price) return false;
            if (oa.remaining_qty != ob.remaining_qty) return false;
            if (oa.cumulative_filled != ob.cumulative_filled) return false;
            if (oa.entry_sequence != ob.entry_sequence) return false;
        }
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════
// DETERMINISTIC REPLAY TESTS
// ═══════════════════════════════════════════════════════════════

/// Core replay proof: process a message sequence on partition A,
/// then replay the same sequence on partition B, and verify
/// identical resulting state.
TEST(ReplayDeterminismTest, IdenticalInputProducesIdenticalState) {
    auto cfg = make_config(100);

    // Build a message sequence
    std::vector<PartitionMessage> messages = {
        make_new_order(100, Side::Sell, 10050, 100, 1),  // Resting sell
        make_new_order(100, Side::Sell, 10060, 200, 2),  // Another sell
        make_new_order(100, Side::Buy, 10040, 150, 3),   // Resting buy (no match)
        make_new_order(100, Side::Buy, 10050, 50, 4),    // Aggressive buy → partial fill
    };

    // ── Run A: primary partition ──────────────────────────────
    JournalStub journal_a(10000);
    Partition part_a(1, 10000, &journal_a);
    part_a.add_instrument(cfg);

    std::vector<MatchResult> results_a;
    for (auto& msg : messages) {
        results_a.push_back(part_a.process(msg));
    }

    auto snap_a = snapshot::capture(part_a);

    // ── Run B: replay partition ───────────────────────────────
    JournalStub journal_b(10000);
    Partition part_b(1, 10000, &journal_b);
    part_b.add_instrument(cfg);

    // Replay the SAME messages from journal_a's input records
    std::vector<MatchResult> results_b;
    journal_a.replay_from(1, [](const JournalEntry& entry, void* ctx) {
        if (entry.entry_type != JournalEntry::TYPE_INPUT_COMMAND) return;

        auto* pair = static_cast<std::pair<Partition*, std::vector<MatchResult>*>*>(ctx);
        PartitionMessage msg;
        std::memcpy(&msg, entry.payload, sizeof(PartitionMessage));
        pair->second->push_back(pair->first->process(msg));
    }, static_cast<void*>(new std::pair<Partition*, std::vector<MatchResult>*>(&part_b, &results_b)));

    auto snap_b = snapshot::capture(part_b);

    // ── Verify: identical state ──────────────────────────────
    EXPECT_TRUE(snapshots_equal(snap_a, snap_b))
        << "Snapshot mismatch: replay did not produce identical book state";

    // Verify sequence counters match
    EXPECT_EQ(part_a.sequence(), part_b.sequence());

    // Verify same number of results
    EXPECT_EQ(results_a.size(), results_b.size());

    // Verify each result action matches
    for (size_t i = 0; i < results_a.size(); ++i) {
        EXPECT_EQ(static_cast<int>(results_a[i].action),
                  static_cast<int>(results_b[i].action))
            << "Result mismatch at message " << i;
    }

    // Verify both journals have the same entry count
    EXPECT_EQ(journal_a.entry_count(), journal_b.entry_count());
}

/// Verify that snapshot capture correctly reflects book state
/// including partial fills and cancellations.
TEST(ReplayDeterminismTest, SnapshotCapturesPartialFillState) {
    auto cfg = make_config(100);
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);
    part.add_instrument(cfg);

    // Sell 100 @ 10050
    part.process(make_new_order(100, Side::Sell, 10050, 100, 1));

    // Buy 40 @ 10050 → partial fill of sell; buy fully consumed
    part.process(make_new_order(100, Side::Buy, 10050, 40, 2));

    auto snap = snapshot::capture(part);

    // Should have exactly 1 resting order: the sell with 60 remaining
    ASSERT_EQ(snap.books.size(), 1u);
    ASSERT_EQ(snap.books[0].orders.size(), 1u);
    EXPECT_EQ(snap.books[0].orders[0].side, Side::Sell);
    EXPECT_EQ(snap.books[0].orders[0].remaining_qty, 60u);
    EXPECT_EQ(snap.books[0].orders[0].cumulative_filled, 40u);
}

/// Verify that cancel removes order from snapshot.
TEST(ReplayDeterminismTest, CancelledOrderNotInSnapshot) {
    auto cfg = make_config(100);
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);
    part.add_instrument(cfg);

    // Insert buy
    part.process(make_new_order(100, Side::Buy, 10000, 100, 1));

    // Capture with 1 resting order
    auto snap1 = snapshot::capture(part);
    EXPECT_EQ(snap1.total_order_count(), 1u);

    // Cancel it (order_id = partition_id:1 << 48 | sequence=1)
    OrderId oid = (static_cast<uint64_t>(1) << 48) | 1;
    part.process(make_cancel(oid));

    // Capture with 0 resting orders
    auto snap2 = snapshot::capture(part);
    EXPECT_EQ(snap2.total_order_count(), 0u);
}

/// Verify journal contains both input and output records.
TEST(ReplayDeterminismTest, JournalContainsInputAndOutput) {
    auto cfg = make_config(100);
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);
    part.add_instrument(cfg);

    part.process(make_new_order(100, Side::Buy, 10000, 100, 1));

    // Each process() call appends 2 entries: 1 input + 1 output
    EXPECT_EQ(journal.entry_count(), 2u);

    const auto& entries = journal.entries();
    EXPECT_EQ(entries[0].entry_type, JournalEntry::TYPE_INPUT_COMMAND);
    EXPECT_EQ(entries[1].entry_type, JournalEntry::TYPE_OUTPUT_EVENT);
}

/// Multi-message determinism: interleaved buys and sells with matches.
TEST(ReplayDeterminismTest, ComplexSequenceReplayIsIdentical) {
    auto cfg = make_config(100);

    std::vector<PartitionMessage> messages = {
        make_new_order(100, Side::Sell, 10050, 100, 1),
        make_new_order(100, Side::Sell, 10060, 200, 2),
        make_new_order(100, Side::Buy, 10030, 50, 3),
        make_new_order(100, Side::Buy, 10050, 75, 4),     // Matches sell @10050 partially
        make_new_order(100, Side::Buy, 10050, 30, 5),     // Takes more from sell @10050
        make_new_order(100, Side::Sell, 10030, 50, 6),    // Matches resting buy @10030
        make_new_order(100, Side::Buy, 10060, 100, 7),    // Takes partial from sell @10060
    };

    // Run primary
    JournalStub j1(10000);
    Partition p1(1, 10000, &j1);
    p1.add_instrument(cfg);
    for (auto& m : messages) p1.process(m);
    auto snap1 = snapshot::capture(p1);

    // Run replay
    JournalStub j2(10000);
    Partition p2(1, 10000, &j2);
    p2.add_instrument(cfg);
    for (auto& m : messages) p2.process(m);
    auto snap2 = snapshot::capture(p2);

    EXPECT_TRUE(snapshots_equal(snap1, snap2));
    EXPECT_EQ(p1.sequence(), p2.sequence());
}

}  // namespace
}  // namespace engine
