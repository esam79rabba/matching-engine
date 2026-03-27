/// Startup controller tests — warm restart, cold start, replay validation.
#include "engine/startup.h"
#include "engine/journal_file.h"
#include "engine/journal_stub.h"
#include <gtest/gtest.h>
#include <cstring>
#include <filesystem>

namespace engine {
namespace {

const std::string TEST_JOURNAL_PATH = "/tmp/matching_engine_startup_test.bin";

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
                             Quantity qty, ClientOrderId clord) {
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::NewOrder;
    msg.timestamp = 1000;
    msg.client_order_id = clord;
    msg.instrument_id = inst;
    msg.side = side;
    msg.order_type = OrderType::Limit;
    msg.time_in_force = TimeInForce::GTC;
    msg.price = price;
    msg.quantity = qty;
    msg.participant_id = 1;
    msg.account_id = 1;
    msg.session_id = 1;
    return msg;
}

class StartupTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::remove(TEST_JOURNAL_PATH.c_str());
    }
    void TearDown() override {
        std::remove(TEST_JOURNAL_PATH.c_str());
    }
};

TEST_F(StartupTest, ColdStartReplaysEntireJournal) {
    // Build a journal with 5 orders
    FileJournal fj;
    ASSERT_TRUE(fj.open(TEST_JOURNAL_PATH, FileJournal::FlushMode::SYNC));

    JournalStub stub(10000);
    Partition source(1, 10000, &stub);
    source.add_instrument(make_config(100));

    // Process orders on source partition (which journals to stub)
    // We need to manually write INPUT entries to the FileJournal for cold start
    for (uint64_t i = 1; i <= 5; ++i) {
        auto msg = make_order(100, Side::Buy, 9990 + i, 10 * i, i);

        JournalEntry entry{};
        entry.sequence = i;
        entry.timestamp = i * 1000;
        entry.partition_id = 1;
        entry.entry_type = JournalEntry::TYPE_INPUT_COMMAND;
        entry.payload_size = sizeof(msg);
        entry.payload = &msg;
        (void)fj.append(entry);
    }
    fj.close();

    // Cold start a fresh partition using the file journal
    FileJournal replay_journal;
    ASSERT_TRUE(replay_journal.open(TEST_JOURNAL_PATH, FileJournal::FlushMode::NONE));

    Partition target(1, 10000);
    target.add_instrument(make_config(100));

    auto result = StartupController::cold_start(target, replay_journal);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.journal_entries_replayed, 5u);
    EXPECT_GT(result.final_sequence, 0u);

    // Verify orders were replayed
    auto* eng = target.get_engine(100);
    ASSERT_NE(eng, nullptr);
    EXPECT_EQ(eng->book().order_count(), 5u);

    replay_journal.close();
}

TEST_F(StartupTest, WarmRestartFromSnapshot) {
    // Create source partition with orders
    JournalStub j1(10000);
    Partition source(1, 10000, &j1);
    source.add_instrument(make_config(100));

    (void)source.process(make_order(100, Side::Buy, 9999, 50, 1));
    (void)source.process(make_order(100, Side::Buy, 9998, 30, 2));
    (void)source.process(make_order(100, Side::Sell, 10050, 80, 3));

    // Capture snapshot at sequence 3
    auto snap = snapshot::capture(source);
    EXPECT_EQ(snap.sequence, 3u);
    EXPECT_EQ(snap.total_order_count(), 3u);

    // Add 2 more orders to the journal AFTER snapshot
    (void)source.process(make_order(100, Side::Buy, 9997, 20, 4));
    (void)source.process(make_order(100, Side::Sell, 10100, 40, 5));

    // Write entries 4 and 5 to a file journal (simulating post-snapshot journal)
    FileJournal fj;
    ASSERT_TRUE(fj.open(TEST_JOURNAL_PATH, FileJournal::FlushMode::SYNC));

    for (uint64_t seq = 4; seq <= 5; ++seq) {
        auto msg = (seq == 4)
            ? make_order(100, Side::Buy, 9997, 20, 4)
            : make_order(100, Side::Sell, 10100, 40, 5);

        JournalEntry entry{};
        entry.sequence = seq;
        entry.timestamp = seq * 1000;
        entry.partition_id = 1;
        entry.entry_type = JournalEntry::TYPE_INPUT_COMMAND;
        entry.payload_size = sizeof(msg);
        entry.payload = &msg;
        (void)fj.append(entry);
    }
    fj.close();

    // Warm restart: snapshot + replay
    FileJournal replay_journal;
    ASSERT_TRUE(replay_journal.open(TEST_JOURNAL_PATH, FileJournal::FlushMode::NONE));

    Partition target(1, 10000);
    target.add_instrument(make_config(100));

    auto result = StartupController::warm_restart(target, snap, replay_journal);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.snapshot_orders_restored, 3u);
    EXPECT_EQ(result.journal_entries_replayed, 2u);

    // Verify: 3 snapshot orders + 2 replayed = 5 total
    auto* eng = target.get_engine(100);
    ASSERT_NE(eng, nullptr);
    EXPECT_EQ(eng->book().order_count(), 5u);

    replay_journal.close();
}

TEST_F(StartupTest, ColdStartEmptyJournal) {
    FileJournal fj;
    ASSERT_TRUE(fj.open(TEST_JOURNAL_PATH, FileJournal::FlushMode::SYNC));
    fj.close();

    FileJournal replay_journal;
    ASSERT_TRUE(replay_journal.open(TEST_JOURNAL_PATH, FileJournal::FlushMode::NONE));

    Partition target(1, 10000);
    target.add_instrument(make_config(100));

    auto result = StartupController::cold_start(target, replay_journal);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.journal_entries_replayed, 0u);

    replay_journal.close();
}

TEST_F(StartupTest, WarmRestartEmptyJournalAfterSnapshot) {
    JournalStub j1(10000);
    Partition source(1, 10000, &j1);
    source.add_instrument(make_config(100));

    (void)source.process(make_order(100, Side::Buy, 9999, 50, 1));
    auto snap = snapshot::capture(source);

    // No journal entries after snapshot
    FileJournal fj;
    ASSERT_TRUE(fj.open(TEST_JOURNAL_PATH, FileJournal::FlushMode::SYNC));
    fj.close();

    FileJournal replay_journal;
    ASSERT_TRUE(replay_journal.open(TEST_JOURNAL_PATH, FileJournal::FlushMode::NONE));

    Partition target(1, 10000);
    target.add_instrument(make_config(100));

    auto result = StartupController::warm_restart(target, snap, replay_journal);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.snapshot_orders_restored, 1u);
    EXPECT_EQ(result.journal_entries_replayed, 0u);

    auto* eng = target.get_engine(100);
    ASSERT_NE(eng, nullptr);
    EXPECT_EQ(eng->book().order_count(), 1u);

    replay_journal.close();
}

TEST_F(StartupTest, ValidateStateOnHealthyPartition) {
    JournalStub j1(10000);
    Partition p(1, 10000, &j1);
    p.add_instrument(make_config(100));
    p.add_instrument(make_config(101));

    (void)p.process(make_order(100, Side::Buy, 9999, 50, 1));
    (void)p.process(make_order(101, Side::Sell, 10050, 30, 2));

    EXPECT_TRUE(StartupController::validate_state(p));
}

}  // namespace
}  // namespace engine
