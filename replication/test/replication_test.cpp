/// DR journal replication tests — replicator, receiver, failover validation.
#include "replication/journal_replicator.h"
#include "replication/journal_receiver.h"
#include "engine/journal_file.h"
#include "engine/journal_stub.h"
#include "engine/partition.h"
#include "engine/startup.h"
#include "engine/matching.h"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <cstring>
#include <filesystem>

namespace replication {
namespace {

// ─── Helpers ─────────────────────────────────────────────────

static engine::InstrumentConfig make_config(engine::InstrumentId id = 100) {
    engine::InstrumentConfig cfg{};
    cfg.instrument_id = id;
    cfg.tick_size = 1;
    cfg.lot_size = 1;
    cfg.min_quantity = 1;
    cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000;
    cfg.static_band_pct = 20.0;
    cfg.residual_policy = engine::ResidualPolicy::Cancel;
    cfg.stp_mode = engine::STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    return cfg;
}

static engine::PartitionMessage make_order(engine::InstrumentId inst,
                                            engine::Side side,
                                            engine::Price price,
                                            engine::Quantity qty,
                                            engine::ClientOrderId clord) {
    engine::PartitionMessage msg{};
    msg.type = engine::PartitionMessage::Type::NewOrder;
    msg.timestamp = 1000;
    msg.client_order_id = clord;
    msg.instrument_id = inst;
    msg.side = side;
    msg.order_type = engine::OrderType::Limit;
    msg.time_in_force = engine::TimeInForce::GTC;
    msg.price = price;
    msg.quantity = qty;
    msg.participant_id = 1;
    msg.account_id = 1;
    msg.session_id = 1;
    return msg;
}

/// Helper: start receiver in a background thread, wait until it is listening.
static std::thread start_receiver_thread(JournalReceiver& receiver) {
    std::thread t([&receiver]() {
        receiver.start();
    });
    // Wait for receiver to come up (poll is_running)
    for (int i = 0; i < 100; ++i) {
        if (receiver.is_running()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Extra small delay for the listen socket to be ready
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return t;
}

// ─── Test fixture ────────────────────────────────────────────

static const std::string PRIMARY_JOURNAL_PATH = "/tmp/repl_test_primary.bin";
static const std::string STANDBY_JOURNAL_PATH = "/tmp/repl_test_standby.bin";

class ReplicationTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::remove(PRIMARY_JOURNAL_PATH.c_str());
        std::remove(STANDBY_JOURNAL_PATH.c_str());
    }
    void TearDown() override {
        std::remove(PRIMARY_JOURNAL_PATH.c_str());
        std::remove(STANDBY_JOURNAL_PATH.c_str());
    }
};

// ─── Tests ───────────────────────────────────────────────────

TEST_F(ReplicationTest, SyncReplicateAndReceive) {
    // Set up standby journal
    engine::FileJournal standby_journal;
    ASSERT_TRUE(standby_journal.open(STANDBY_JOURNAL_PATH, engine::FileJournal::FlushMode::NONE));

    // Start receiver on random port
    JournalReceiver::Config rcfg;
    rcfg.listen_port = 0;  // OS-assigned port
    JournalReceiver receiver(rcfg, &standby_journal);
    auto recv_thread = start_receiver_thread(receiver);

    uint16_t port = receiver.bound_port();
    ASSERT_GT(port, 0);

    // Create replicator pointing to receiver
    JournalReplicator::Config scfg;
    scfg.dr_host = "127.0.0.1";
    scfg.dr_port = port;
    scfg.mode = ReplicationMode::SYNC;
    JournalReplicator replicator(scfg);
    ASSERT_TRUE(replicator.connect());

    // Replicate 10 entries
    for (uint64_t i = 1; i <= 10; ++i) {
        auto msg = make_order(100, engine::Side::Buy, 9990 + static_cast<int64_t>(i), i * 10, i);

        engine::JournalEntry entry{};
        entry.sequence = i;
        entry.timestamp = i * 1000;
        entry.partition_id = 1;
        entry.entry_type = engine::JournalEntry::TYPE_INPUT_COMMAND;
        entry.payload_size = sizeof(msg);
        entry.payload = &msg;

        EXPECT_TRUE(replicator.replicate(entry)) << "Failed to replicate entry " << i;
    }

    replicator.disconnect();
    receiver.stop();
    recv_thread.join();

    // Verify standby journal has 10 entries
    EXPECT_EQ(standby_journal.entry_count(), 10u);
    EXPECT_EQ(standby_journal.last_sequence(), 10u);

    standby_journal.close();
}

TEST_F(ReplicationTest, CrcVerification) {
    engine::FileJournal standby_journal;
    ASSERT_TRUE(standby_journal.open(STANDBY_JOURNAL_PATH, engine::FileJournal::FlushMode::SYNC));

    JournalReceiver::Config rcfg;
    rcfg.listen_port = 0;
    JournalReceiver receiver(rcfg, &standby_journal);
    auto recv_thread = start_receiver_thread(receiver);

    JournalReplicator::Config scfg;
    scfg.dr_host = "127.0.0.1";
    scfg.dr_port = receiver.bound_port();
    scfg.mode = ReplicationMode::SYNC;
    JournalReplicator replicator(scfg);
    ASSERT_TRUE(replicator.connect());

    // Replicate 5 entries
    for (uint64_t i = 1; i <= 5; ++i) {
        auto msg = make_order(100, engine::Side::Buy, 10000, 100, i);
        engine::JournalEntry entry{};
        entry.sequence = i;
        entry.timestamp = i * 1000;
        entry.partition_id = 1;
        entry.entry_type = engine::JournalEntry::TYPE_INPUT_COMMAND;
        entry.payload_size = sizeof(msg);
        entry.payload = &msg;
        EXPECT_TRUE(replicator.replicate(entry));
    }

    replicator.disconnect();
    receiver.stop();
    recv_thread.join();
    standby_journal.close();

    // Verify integrity of the standby journal file (CRC check)
    engine::FileJournal verify_journal;
    ASSERT_TRUE(verify_journal.open(STANDBY_JOURNAL_PATH, engine::FileJournal::FlushMode::NONE));
    EXPECT_EQ(verify_journal.verify_integrity(), 5u);
    verify_journal.close();
}

TEST_F(ReplicationTest, SequenceContinuity) {
    engine::FileJournal standby_journal;
    ASSERT_TRUE(standby_journal.open(STANDBY_JOURNAL_PATH, engine::FileJournal::FlushMode::NONE));

    JournalReceiver::Config rcfg;
    rcfg.listen_port = 0;
    JournalReceiver receiver(rcfg, &standby_journal);
    auto recv_thread = start_receiver_thread(receiver);

    JournalReplicator::Config scfg;
    scfg.dr_host = "127.0.0.1";
    scfg.dr_port = receiver.bound_port();
    scfg.mode = ReplicationMode::SYNC;
    JournalReplicator replicator(scfg);
    ASSERT_TRUE(replicator.connect());

    // Replicate 5 sequential entries
    for (uint64_t i = 1; i <= 5; ++i) {
        auto msg = make_order(100, engine::Side::Buy, 10000, 100, i);
        engine::JournalEntry entry{};
        entry.sequence = i;
        entry.timestamp = i * 1000;
        entry.partition_id = 1;
        entry.entry_type = engine::JournalEntry::TYPE_INPUT_COMMAND;
        entry.payload_size = sizeof(msg);
        entry.payload = &msg;
        EXPECT_TRUE(replicator.replicate(entry));
    }

    replicator.disconnect();
    receiver.stop();
    recv_thread.join();

    // Receiver tracked sequence correctly — no gaps
    auto rstats = receiver.stats();
    EXPECT_EQ(rstats.sequence_gaps, 0u);
    EXPECT_EQ(rstats.last_received_seq, 5u);

    standby_journal.close();
}

TEST_F(ReplicationTest, ReplicationStats) {
    engine::FileJournal standby_journal;
    ASSERT_TRUE(standby_journal.open(STANDBY_JOURNAL_PATH, engine::FileJournal::FlushMode::NONE));

    JournalReceiver::Config rcfg;
    rcfg.listen_port = 0;
    JournalReceiver receiver(rcfg, &standby_journal);
    auto recv_thread = start_receiver_thread(receiver);

    JournalReplicator::Config scfg;
    scfg.dr_host = "127.0.0.1";
    scfg.dr_port = receiver.bound_port();
    scfg.mode = ReplicationMode::SYNC;
    JournalReplicator replicator(scfg);
    ASSERT_TRUE(replicator.connect());

    for (uint64_t i = 1; i <= 10; ++i) {
        auto msg = make_order(100, engine::Side::Buy, 10000, 100, i);
        engine::JournalEntry entry{};
        entry.sequence = i;
        entry.timestamp = i * 1000;
        entry.partition_id = 1;
        entry.entry_type = engine::JournalEntry::TYPE_INPUT_COMMAND;
        entry.payload_size = sizeof(msg);
        entry.payload = &msg;
        EXPECT_TRUE(replicator.replicate(entry));
    }

    auto sstats = replicator.stats();
    EXPECT_EQ(sstats.entries_sent, 10u);
    EXPECT_EQ(sstats.entries_acked, 10u);
    EXPECT_EQ(sstats.last_acked_seq, 10u);
    EXPECT_EQ(sstats.ack_failures, 0u);
    EXPECT_TRUE(sstats.connected);

    // Each entry: 24 (hdr) + sizeof(PartitionMessage) (payload) + 4 (crc)
    uint64_t expected_bytes = 10 * (24 + sizeof(engine::PartitionMessage) + 4);
    EXPECT_EQ(sstats.bytes_sent, expected_bytes);

    replicator.disconnect();
    receiver.stop();
    recv_thread.join();
    standby_journal.close();
}

TEST_F(ReplicationTest, ReceiverStats) {
    engine::FileJournal standby_journal;
    ASSERT_TRUE(standby_journal.open(STANDBY_JOURNAL_PATH, engine::FileJournal::FlushMode::NONE));

    JournalReceiver::Config rcfg;
    rcfg.listen_port = 0;
    JournalReceiver receiver(rcfg, &standby_journal);
    auto recv_thread = start_receiver_thread(receiver);

    JournalReplicator::Config scfg;
    scfg.dr_host = "127.0.0.1";
    scfg.dr_port = receiver.bound_port();
    scfg.mode = ReplicationMode::SYNC;
    JournalReplicator replicator(scfg);
    ASSERT_TRUE(replicator.connect());

    for (uint64_t i = 1; i <= 7; ++i) {
        auto msg = make_order(100, engine::Side::Buy, 10000, 100, i);
        engine::JournalEntry entry{};
        entry.sequence = i;
        entry.timestamp = i * 1000;
        entry.partition_id = 1;
        entry.entry_type = engine::JournalEntry::TYPE_INPUT_COMMAND;
        entry.payload_size = sizeof(msg);
        entry.payload = &msg;
        EXPECT_TRUE(replicator.replicate(entry));
    }

    replicator.disconnect();
    receiver.stop();
    recv_thread.join();

    auto rstats = receiver.stats();
    EXPECT_EQ(rstats.entries_received, 7u);
    EXPECT_EQ(rstats.entries_persisted, 7u);
    EXPECT_EQ(rstats.crc_failures, 0u);
    EXPECT_EQ(rstats.sequence_gaps, 0u);
    EXPECT_EQ(rstats.last_received_seq, 7u);

    standby_journal.close();
}

TEST_F(ReplicationTest, DisconnectReconnect) {
    engine::FileJournal standby_journal;
    ASSERT_TRUE(standby_journal.open(STANDBY_JOURNAL_PATH, engine::FileJournal::FlushMode::NONE));

    JournalReceiver::Config rcfg;
    rcfg.listen_port = 0;
    JournalReceiver receiver(rcfg, &standby_journal);
    auto recv_thread = start_receiver_thread(receiver);

    JournalReplicator::Config scfg;
    scfg.dr_host = "127.0.0.1";
    scfg.dr_port = receiver.bound_port();
    scfg.mode = ReplicationMode::SYNC;
    JournalReplicator replicator(scfg);
    ASSERT_TRUE(replicator.connect());

    // Send 3 entries
    for (uint64_t i = 1; i <= 3; ++i) {
        auto msg = make_order(100, engine::Side::Buy, 10000, 100, i);
        engine::JournalEntry entry{};
        entry.sequence = i;
        entry.timestamp = i * 1000;
        entry.partition_id = 1;
        entry.entry_type = engine::JournalEntry::TYPE_INPUT_COMMAND;
        entry.payload_size = sizeof(msg);
        entry.payload = &msg;
        EXPECT_TRUE(replicator.replicate(entry));
    }

    // Disconnect
    replicator.disconnect();
    EXPECT_FALSE(replicator.is_connected());

    // Wait a moment for receiver to detect disconnect and return to accept loop
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Reconnect
    ASSERT_TRUE(replicator.connect());
    EXPECT_TRUE(replicator.is_connected());

    // Send 2 more entries (continuing sequence)
    for (uint64_t i = 4; i <= 5; ++i) {
        auto msg = make_order(100, engine::Side::Buy, 10000, 100, i);
        engine::JournalEntry entry{};
        entry.sequence = i;
        entry.timestamp = i * 1000;
        entry.partition_id = 1;
        entry.entry_type = engine::JournalEntry::TYPE_INPUT_COMMAND;
        entry.payload_size = sizeof(msg);
        entry.payload = &msg;
        EXPECT_TRUE(replicator.replicate(entry));
    }

    replicator.disconnect();
    receiver.stop();
    recv_thread.join();

    // Total: 5 entries persisted
    EXPECT_EQ(standby_journal.entry_count(), 5u);

    standby_journal.close();
}

TEST_F(ReplicationTest, SimulatedFailover) {
    // This is the key test: prove that a DR journal replicated over TCP
    // can be used by StartupController::cold_start to recover partition state.

    // Step 1: Set up primary partition + journal
    engine::FileJournal primary_journal;
    ASSERT_TRUE(primary_journal.open(PRIMARY_JOURNAL_PATH, engine::FileJournal::FlushMode::NONE));

    engine::Partition primary_partition(1, 10000, &primary_journal);
    primary_partition.add_instrument(make_config(100));

    // Step 2: Set up standby journal + receiver
    engine::FileJournal standby_journal;
    ASSERT_TRUE(standby_journal.open(STANDBY_JOURNAL_PATH, engine::FileJournal::FlushMode::NONE));

    JournalReceiver::Config rcfg;
    rcfg.listen_port = 0;
    JournalReceiver receiver(rcfg, &standby_journal);
    auto recv_thread = start_receiver_thread(receiver);

    // Step 3: Set up replicator
    JournalReplicator::Config scfg;
    scfg.dr_host = "127.0.0.1";
    scfg.dr_port = receiver.bound_port();
    scfg.mode = ReplicationMode::SYNC;
    JournalReplicator replicator(scfg);
    ASSERT_TRUE(replicator.connect());

    // Step 4: Process orders on primary partition.
    // The partition writes INPUT + OUTPUT entries to primary_journal.
    // We need to replicate INPUT entries to standby (cold_start only replays INPUT).
    // Strategy: process on primary, then replay primary journal entries to replicator.
    auto order1 = make_order(100, engine::Side::Buy, 9990, 50, 1);
    auto order2 = make_order(100, engine::Side::Buy, 9980, 30, 2);
    auto order3 = make_order(100, engine::Side::Sell, 10050, 80, 3);
    auto order4 = make_order(100, engine::Side::Buy, 9970, 20, 4);
    auto order5 = make_order(100, engine::Side::Sell, 10100, 40, 5);

    (void)primary_partition.process(order1);
    (void)primary_partition.process(order2);
    (void)primary_partition.process(order3);
    (void)primary_partition.process(order4);
    (void)primary_partition.process(order5);

    // Step 5: Flush primary journal so replay_from can read entries from disk
    primary_journal.flush();

    // Replicate all journal entries from primary to standby
    // Use replay_from to read back all entries from the primary journal
    struct ReplicateCtx {
        JournalReplicator* replicator;
        uint64_t replicated;
    };
    ReplicateCtx rctx{&replicator, 0};

    primary_journal.replay_from(1, [](const engine::JournalEntry& entry, void* ctx) {
        auto* c = static_cast<ReplicateCtx*>(ctx);
        c->replicator->replicate(entry);
        c->replicated++;
    }, &rctx);

    EXPECT_GT(rctx.replicated, 0u);

    // Step 6: Stop replication
    replicator.disconnect();
    receiver.stop();
    recv_thread.join();

    // Step 7: Verify standby journal has entries
    EXPECT_GT(standby_journal.entry_count(), 0u);

    // Step 8: Close and reopen standby journal for cold start
    standby_journal.close();
    engine::FileJournal recovery_journal;
    ASSERT_TRUE(recovery_journal.open(STANDBY_JOURNAL_PATH, engine::FileJournal::FlushMode::NONE));

    // Step 9: Cold start a new partition from the standby journal
    engine::Partition dr_partition(1, 10000);
    dr_partition.add_instrument(make_config(100));

    auto startup_result = engine::StartupController::cold_start(dr_partition, recovery_journal);
    EXPECT_TRUE(startup_result.success) << "Cold start failed: " << startup_result.error;
    EXPECT_GT(startup_result.journal_entries_replayed, 0u);

    // Step 10: Verify DR partition matches primary
    // Both should have the same order books
    auto* primary_eng = primary_partition.get_engine(100);
    auto* dr_eng = dr_partition.get_engine(100);
    ASSERT_NE(primary_eng, nullptr);
    ASSERT_NE(dr_eng, nullptr);

    EXPECT_EQ(primary_eng->book().order_count(), dr_eng->book().order_count());
    EXPECT_EQ(primary_eng->book().best_bid(), dr_eng->book().best_bid());
    EXPECT_EQ(primary_eng->book().best_ask(), dr_eng->book().best_ask());

    // Validate DR partition state
    EXPECT_TRUE(engine::StartupController::validate_state(dr_partition));

    recovery_journal.close();
    primary_journal.close();
}

TEST_F(ReplicationTest, ReplicationLagMetric) {
    engine::FileJournal standby_journal;
    ASSERT_TRUE(standby_journal.open(STANDBY_JOURNAL_PATH, engine::FileJournal::FlushMode::NONE));

    JournalReceiver::Config rcfg;
    rcfg.listen_port = 0;
    JournalReceiver receiver(rcfg, &standby_journal);
    auto recv_thread = start_receiver_thread(receiver);

    JournalReplicator::Config scfg;
    scfg.dr_host = "127.0.0.1";
    scfg.dr_port = receiver.bound_port();
    scfg.mode = ReplicationMode::SYNC;
    JournalReplicator replicator(scfg);
    ASSERT_TRUE(replicator.connect());

    // Replicate 10 entries
    for (uint64_t i = 1; i <= 10; ++i) {
        auto msg = make_order(100, engine::Side::Buy, 10000, 100, i);
        engine::JournalEntry entry{};
        entry.sequence = i;
        entry.timestamp = i * 1000;
        entry.partition_id = 1;
        entry.entry_type = engine::JournalEntry::TYPE_INPUT_COMMAND;
        entry.payload_size = sizeof(msg);
        entry.payload = &msg;
        EXPECT_TRUE(replicator.replicate(entry));
    }

    // In sync mode, lag should be zero (all acked)
    auto sstats = replicator.stats();
    uint64_t lag = sstats.entries_sent - sstats.entries_acked;
    EXPECT_EQ(lag, 0u);
    EXPECT_EQ(sstats.entries_sent, 10u);
    EXPECT_EQ(sstats.entries_acked, 10u);

    replicator.disconnect();
    receiver.stop();
    recv_thread.join();
    standby_journal.close();
}

}  // namespace
}  // namespace replication
