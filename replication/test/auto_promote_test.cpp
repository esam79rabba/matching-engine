/// Auto-promote controller tests — generation counter, automated failover, end-to-end simulation.
#include "replication/auto_promote_controller.h"
#include "replication/generation_counter.h"
#include "replication/health_probe.h"
#include "replication/failover_controller.h"
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
#include <cstdio>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

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

/// Start receiver in a background thread, wait until it is listening.
static std::thread start_receiver_thread(JournalReceiver& receiver) {
    std::thread t([&receiver]() {
        receiver.start();
    });
    for (int i = 0; i < 100; ++i) {
        if (receiver.is_running()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return t;
}

// ─── GenerationCounter Tests ─────────────────────────────────

TEST(GenerationCounterTest, IncrementAndPersist) {
    const std::string path = "/tmp/gen_counter_test.txt";
    std::remove(path.c_str());

    {
        GenerationCounter counter(path);
        EXPECT_EQ(counter.current(), 0u);

        uint64_t gen1 = counter.increment();
        EXPECT_EQ(gen1, 1u);
        EXPECT_EQ(counter.current(), 1u);

        uint64_t gen2 = counter.increment();
        EXPECT_EQ(gen2, 2u);
        EXPECT_EQ(counter.current(), 2u);
    }

    // Create a new counter on the same file — should load persisted value
    {
        GenerationCounter counter2(path);
        EXPECT_EQ(counter2.current(), 2u);

        uint64_t gen3 = counter2.increment();
        EXPECT_EQ(gen3, 3u);
    }

    std::remove(path.c_str());
}

TEST(GenerationCounterTest, StaleDetection) {
    GenerationCounter counter;  // no persistence

    // Increment to generation 5
    for (int i = 0; i < 5; ++i) {
        counter.increment();
    }
    EXPECT_EQ(counter.current(), 5u);

    // Stale checks
    EXPECT_TRUE(counter.is_stale(3));   // 3 < 5 → stale
    EXPECT_FALSE(counter.is_stale(5));  // 5 == 5 → not stale (current)
    EXPECT_FALSE(counter.is_stale(6));  // 6 > 5 → not stale (future)

    // Current checks
    EXPECT_FALSE(counter.is_current(3));
    EXPECT_TRUE(counter.is_current(5));
    EXPECT_FALSE(counter.is_current(6));
}

TEST(GenerationCounterTest, LoadFromMissingFile) {
    const std::string path = "/tmp/gen_counter_missing.txt";
    std::remove(path.c_str());

    GenerationCounter counter(path);
    EXPECT_EQ(counter.current(), 0u);

    std::remove(path.c_str());
}

// ─── AutoPromoteController Tests ─────────────────────────────

TEST(AutoPromoteTest, PromotesWhenAllConditionsMet) {
    // Set up primary journal with entries, replicate to standby
    const std::string primary_path = "/tmp/auto_promote_primary.bin";
    const std::string standby_path = "/tmp/auto_promote_standby.bin";
    std::remove(primary_path.c_str());
    std::remove(standby_path.c_str());

    // Primary: journal + partition + orders
    engine::FileJournal primary_journal;
    ASSERT_TRUE(primary_journal.open(primary_path, engine::FileJournal::FlushMode::NONE));
    engine::Partition primary_partition(1, 10000, &primary_journal);
    primary_partition.add_instrument(make_config(100));

    auto order1 = make_order(100, engine::Side::Buy, 9990, 50, 1);
    auto order2 = make_order(100, engine::Side::Buy, 9980, 30, 2);
    (void)primary_partition.process(order1);
    (void)primary_partition.process(order2);
    primary_journal.flush();

    // Standby: journal + receiver
    engine::FileJournal standby_journal;
    ASSERT_TRUE(standby_journal.open(standby_path, engine::FileJournal::FlushMode::NONE));

    JournalReceiver::Config rcfg;
    rcfg.listen_port = 0;
    JournalReceiver receiver(rcfg, &standby_journal);
    auto recv_thread = start_receiver_thread(receiver);

    // Replicate
    JournalReplicator::Config scfg;
    scfg.dr_host = "127.0.0.1";
    scfg.dr_port = receiver.bound_port();
    scfg.mode = ReplicationMode::SYNC;
    JournalReplicator replicator(scfg);
    ASSERT_TRUE(replicator.connect());

    primary_journal.replay_from(1, [](const engine::JournalEntry& e, void* ctx) {
        auto* r = static_cast<JournalReplicator*>(ctx);
        r->replicate(e);
    }, &replicator);

    replicator.disconnect();
    receiver.stop();
    recv_thread.join();
    EXPECT_GT(standby_journal.entry_count(), 0u);

    // Close and reopen standby journal for recovery
    standby_journal.close();
    engine::FileJournal recovery_journal;
    ASSERT_TRUE(recovery_journal.open(standby_path, engine::FileJournal::FlushMode::NONE));

    // Probe in FAILED state
    HealthProbe::Config pcfg;
    pcfg.primary_port = 19990;
    HealthProbe probe(pcfg);
    probe.force_state(HealthProbe::State::FAILED);

    // Receiver for controller (reuse stats from the one that already received)
    // Create a new receiver that reports entries matching what we replicated
    engine::JournalStub dummy_stub;
    JournalReceiver::Config dummy_rcfg;
    dummy_rcfg.listen_port = 0;
    JournalReceiver dummy_receiver(dummy_rcfg, &dummy_stub);

    // We need the receiver to report entries_received > 0.
    // The receiver tracks entries in its stats. We'll use a trick:
    // start receiver, send entries to it, stop it.
    auto dummy_recv_thread = start_receiver_thread(dummy_receiver);

    JournalReplicator::Config dummy_scfg;
    dummy_scfg.dr_host = "127.0.0.1";
    dummy_scfg.dr_port = dummy_receiver.bound_port();
    dummy_scfg.mode = ReplicationMode::SYNC;
    JournalReplicator dummy_replicator(dummy_scfg);
    ASSERT_TRUE(dummy_replicator.connect());

    // Replicate at least one entry so stats show entries_received > 0
    recovery_journal.replay_from(1, [](const engine::JournalEntry& e, void* ctx) {
        auto* r = static_cast<JournalReplicator*>(ctx);
        r->replicate(e);
    }, &dummy_replicator);

    dummy_replicator.disconnect();
    dummy_receiver.stop();
    dummy_recv_thread.join();

    auto rstats = dummy_receiver.stats();
    EXPECT_GT(rstats.entries_received, 0u);

    // Failover controller
    FailoverController failover_ctrl(&probe, &dummy_receiver);

    // Generation counter
    const std::string gen_path = "/tmp/auto_promote_gen.txt";
    std::remove(gen_path.c_str());
    GenerationCounter generation(gen_path);
    EXPECT_EQ(generation.current(), 0u);

    // AutoPromoteController
    AutoPromoteConfig auto_cfg;
    auto_cfg.enabled = true;
    auto_cfg.promote_delay_ms = 100;  // Short delay for testing
    auto_cfg.min_receiver_entries = 1;

    AutoPromoteController controller(auto_cfg, &probe, &failover_ctrl, &generation, &dummy_receiver);

    // Reopen recovery journal for the actual promote (it was read during dummy replication)
    recovery_journal.close();
    engine::FileJournal promote_journal;
    ASSERT_TRUE(promote_journal.open(standby_path, engine::FileJournal::FlushMode::NONE));

    engine::Partition dr_partition(1, 10000);
    std::vector<engine::InstrumentConfig> instruments = {make_config(100)};
    controller.set_promotion_target(&promote_journal, &dr_partition, instruments);

    // Track audit events
    std::mutex audit_mtx;
    std::vector<std::string> audit_actions;
    controller.set_audit_callback([&](const std::string& action, const std::string& /*details*/) {
        std::lock_guard<std::mutex> lock(audit_mtx);
        audit_actions.push_back(action);
    });

    controller.start();

    // Wait for promotion (delay + processing)
    for (int i = 0; i < 100 && controller.is_running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto decision = controller.last_decision();
    EXPECT_TRUE(decision.promoted) << "Reason: " << decision.reason;
    EXPECT_TRUE(decision.attempted);
    EXPECT_EQ(decision.new_generation, 1u);
    EXPECT_GT(decision.entries_replayed, 0u);

    controller.stop();
    promote_journal.close();
    primary_journal.close();
    std::remove(primary_path.c_str());
    std::remove(standby_path.c_str());
    std::remove(gen_path.c_str());
}

TEST(AutoPromoteTest, DoesNotPromoteWhenDisabled) {
    engine::JournalStub stub_journal;
    JournalReceiver::Config rcfg;
    rcfg.listen_port = 0;
    JournalReceiver receiver(rcfg, &stub_journal);

    HealthProbe::Config pcfg;
    pcfg.primary_port = 19989;
    HealthProbe probe(pcfg);
    probe.force_state(HealthProbe::State::FAILED);

    FailoverController failover_ctrl(&probe, &receiver);
    GenerationCounter generation;

    AutoPromoteConfig auto_cfg;
    auto_cfg.enabled = false;  // Disabled
    auto_cfg.promote_delay_ms = 100;

    AutoPromoteController controller(auto_cfg, &probe, &failover_ctrl, &generation, &receiver);

    std::mutex audit_mtx;
    std::vector<std::string> audit_actions;
    controller.set_audit_callback([&](const std::string& action, const std::string& /*details*/) {
        std::lock_guard<std::mutex> lock(audit_mtx);
        audit_actions.push_back(action);
    });

    controller.start();
    // Wait enough time for the monitor loop to detect FAILED and decide
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    controller.stop();

    auto decision = controller.last_decision();
    EXPECT_FALSE(decision.promoted);

    // Should have seen detection but then disabled message
    std::lock_guard<std::mutex> lock(audit_mtx);
    bool saw_disabled = false;
    for (const auto& a : audit_actions) {
        if (a == "auto_promote_disabled") {
            saw_disabled = true;
            break;
        }
    }
    EXPECT_TRUE(saw_disabled);
}

TEST(AutoPromoteTest, DoesNotPromoteWhenHealthy) {
    engine::JournalStub stub_journal;
    JournalReceiver::Config rcfg;
    rcfg.listen_port = 0;
    JournalReceiver receiver(rcfg, &stub_journal);

    HealthProbe::Config pcfg;
    pcfg.primary_port = 19988;
    HealthProbe probe(pcfg);
    // Default state: HEALTHY

    FailoverController failover_ctrl(&probe, &receiver);
    GenerationCounter generation;

    AutoPromoteConfig auto_cfg;
    auto_cfg.enabled = true;
    auto_cfg.promote_delay_ms = 100;

    AutoPromoteController controller(auto_cfg, &probe, &failover_ctrl, &generation, &receiver);
    controller.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    controller.stop();

    auto decision = controller.last_decision();
    EXPECT_FALSE(decision.promoted);
    EXPECT_FALSE(decision.attempted);
}

TEST(AutoPromoteTest, DoesNotPromoteWhenNoEntries) {
    engine::JournalStub stub_journal;
    JournalReceiver::Config rcfg;
    rcfg.listen_port = 0;
    JournalReceiver receiver(rcfg, &stub_journal);
    // receiver has 0 entries (never started)

    HealthProbe::Config pcfg;
    pcfg.primary_port = 19987;
    HealthProbe probe(pcfg);
    probe.force_state(HealthProbe::State::FAILED);

    FailoverController failover_ctrl(&probe, &receiver);
    GenerationCounter generation;

    AutoPromoteConfig auto_cfg;
    auto_cfg.enabled = true;
    auto_cfg.promote_delay_ms = 100;
    auto_cfg.min_receiver_entries = 1;

    AutoPromoteController controller(auto_cfg, &probe, &failover_ctrl, &generation, &receiver);

    engine::Partition partition(1, 10000);
    std::vector<engine::InstrumentConfig> instruments = {make_config(100)};
    controller.set_promotion_target(&stub_journal, &partition, instruments);

    controller.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    controller.stop();

    auto decision = controller.last_decision();
    EXPECT_FALSE(decision.promoted);
    // Should have attempted but failed due to readiness or entry check
    if (decision.attempted) {
        EXPECT_FALSE(decision.reason.empty());
    }
}

TEST(AutoPromoteTest, GenerationIncrementsOnPromote) {
    // Set up a minimal replication scenario to test generation increment
    const std::string primary_path = "/tmp/auto_gen_primary.bin";
    const std::string standby_path = "/tmp/auto_gen_standby.bin";
    const std::string gen_path = "/tmp/auto_gen_counter.txt";
    std::remove(primary_path.c_str());
    std::remove(standby_path.c_str());
    std::remove(gen_path.c_str());

    // Primary
    engine::FileJournal primary_journal;
    ASSERT_TRUE(primary_journal.open(primary_path, engine::FileJournal::FlushMode::NONE));
    engine::Partition primary_partition(1, 10000, &primary_journal);
    primary_partition.add_instrument(make_config(100));

    auto order = make_order(100, engine::Side::Buy, 9990, 50, 1);
    (void)primary_partition.process(order);
    primary_journal.flush();

    // Standby
    engine::FileJournal standby_journal;
    ASSERT_TRUE(standby_journal.open(standby_path, engine::FileJournal::FlushMode::NONE));

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

    primary_journal.replay_from(1, [](const engine::JournalEntry& e, void* ctx) {
        auto* r = static_cast<JournalReplicator*>(ctx);
        r->replicate(e);
    }, &replicator);

    replicator.disconnect();
    receiver.stop();
    recv_thread.join();

    standby_journal.close();
    engine::FileJournal recovery_journal;
    ASSERT_TRUE(recovery_journal.open(standby_path, engine::FileJournal::FlushMode::NONE));

    // Set up controller
    HealthProbe::Config pcfg;
    pcfg.primary_port = 19986;
    HealthProbe probe(pcfg);
    probe.force_state(HealthProbe::State::FAILED);

    // Create receiver with entries for the controller
    engine::JournalStub ctrl_stub;
    JournalReceiver::Config ctrl_rcfg;
    ctrl_rcfg.listen_port = 0;
    JournalReceiver ctrl_receiver(ctrl_rcfg, &ctrl_stub);
    auto ctrl_recv_thread = start_receiver_thread(ctrl_receiver);

    JournalReplicator::Config ctrl_scfg;
    ctrl_scfg.dr_host = "127.0.0.1";
    ctrl_scfg.dr_port = ctrl_receiver.bound_port();
    ctrl_scfg.mode = ReplicationMode::SYNC;
    JournalReplicator ctrl_replicator(ctrl_scfg);
    ASSERT_TRUE(ctrl_replicator.connect());

    recovery_journal.replay_from(1, [](const engine::JournalEntry& e, void* ctx) {
        auto* r = static_cast<JournalReplicator*>(ctx);
        r->replicate(e);
    }, &ctrl_replicator);

    ctrl_replicator.disconnect();
    ctrl_receiver.stop();
    ctrl_recv_thread.join();

    FailoverController failover_ctrl(&probe, &ctrl_receiver);
    GenerationCounter generation(gen_path);
    EXPECT_EQ(generation.current(), 0u);

    AutoPromoteConfig auto_cfg;
    auto_cfg.enabled = true;
    auto_cfg.promote_delay_ms = 100;
    auto_cfg.min_receiver_entries = 1;

    AutoPromoteController controller(auto_cfg, &probe, &failover_ctrl, &generation, &ctrl_receiver);

    recovery_journal.close();
    engine::FileJournal promote_journal;
    ASSERT_TRUE(promote_journal.open(standby_path, engine::FileJournal::FlushMode::NONE));

    engine::Partition dr_partition(1, 10000);
    std::vector<engine::InstrumentConfig> instruments = {make_config(100)};
    controller.set_promotion_target(&promote_journal, &dr_partition, instruments);

    controller.start();
    for (int i = 0; i < 100 && controller.is_running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    controller.stop();

    auto decision = controller.last_decision();
    EXPECT_TRUE(decision.promoted) << "Reason: " << decision.reason;
    EXPECT_EQ(generation.current(), 1u);
    EXPECT_EQ(decision.new_generation, 1u);

    promote_journal.close();
    primary_journal.close();
    std::remove(primary_path.c_str());
    std::remove(standby_path.c_str());
    std::remove(gen_path.c_str());
}

TEST(AutoPromoteTest, AuditCallbackFires) {
    // Minimal setup to verify audit callbacks fire on failover detection
    engine::JournalStub stub_journal;
    JournalReceiver::Config rcfg;
    rcfg.listen_port = 0;
    JournalReceiver receiver(rcfg, &stub_journal);

    HealthProbe::Config pcfg;
    pcfg.primary_port = 19985;
    HealthProbe probe(pcfg);
    probe.force_state(HealthProbe::State::FAILED);

    FailoverController failover_ctrl(&probe, &receiver);
    GenerationCounter generation;

    AutoPromoteConfig auto_cfg;
    auto_cfg.enabled = false;  // Disabled so we can just check audit without full promote
    auto_cfg.promote_delay_ms = 100;

    AutoPromoteController controller(auto_cfg, &probe, &failover_ctrl, &generation, &receiver);

    std::mutex audit_mtx;
    std::vector<std::pair<std::string, std::string>> audit_events;
    controller.set_audit_callback([&](const std::string& action, const std::string& details) {
        std::lock_guard<std::mutex> lock(audit_mtx);
        audit_events.emplace_back(action, details);
    });

    controller.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    controller.stop();

    std::lock_guard<std::mutex> lock(audit_mtx);
    ASSERT_FALSE(audit_events.empty());

    // Should see failover_detection
    bool saw_detection = false;
    bool saw_disabled = false;
    for (const auto& [action, details] : audit_events) {
        if (action == "failover_detection") saw_detection = true;
        if (action == "auto_promote_disabled") saw_disabled = true;
    }
    EXPECT_TRUE(saw_detection);
    EXPECT_TRUE(saw_disabled);
}

TEST(AutoPromoteTest, EndToEndFailoverSimulation) {
    // Full end-to-end test:
    // 1. Create primary partition + journal, process 5 orders
    // 2. Replicate all entries to standby
    // 3. Set probe to FAILED
    // 4. Start AutoPromoteController
    // 5. Verify promotion and state match

    const std::string primary_path = "/tmp/e2e_failover_primary.bin";
    const std::string standby_path = "/tmp/e2e_failover_standby.bin";
    const std::string gen_path = "/tmp/e2e_failover_gen.txt";
    std::remove(primary_path.c_str());
    std::remove(standby_path.c_str());
    std::remove(gen_path.c_str());

    // Step 1: Primary journal + partition, process 5 orders
    engine::FileJournal primary_journal;
    ASSERT_TRUE(primary_journal.open(primary_path, engine::FileJournal::FlushMode::NONE));
    engine::Partition primary_partition(1, 10000, &primary_journal);
    primary_partition.add_instrument(make_config(100));

    auto o1 = make_order(100, engine::Side::Buy, 9990, 50, 1);
    auto o2 = make_order(100, engine::Side::Buy, 9980, 30, 2);
    auto o3 = make_order(100, engine::Side::Buy, 9970, 20, 3);
    auto o4 = make_order(100, engine::Side::Sell, 10050, 80, 4);
    auto o5 = make_order(100, engine::Side::Sell, 10060, 40, 5);
    (void)primary_partition.process(o1);
    (void)primary_partition.process(o2);
    (void)primary_partition.process(o3);
    (void)primary_partition.process(o4);
    (void)primary_partition.process(o5);
    primary_journal.flush();

    // Step 2: Replicate to standby via JournalReplicator + JournalReceiver
    engine::FileJournal standby_journal;
    ASSERT_TRUE(standby_journal.open(standby_path, engine::FileJournal::FlushMode::NONE));

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

    primary_journal.replay_from(1, [](const engine::JournalEntry& e, void* ctx) {
        auto* r = static_cast<JournalReplicator*>(ctx);
        r->replicate(e);
    }, &replicator);

    replicator.disconnect();
    receiver.stop();
    recv_thread.join();
    EXPECT_GT(standby_journal.entry_count(), 0u);

    // Close standby journal for recovery
    standby_journal.close();
    engine::FileJournal recovery_journal;
    ASSERT_TRUE(recovery_journal.open(standby_path, engine::FileJournal::FlushMode::NONE));

    // Step 3: Set probe to FAILED (simulating primary failure)
    HealthProbe::Config pcfg;
    pcfg.primary_port = 19984;
    HealthProbe probe(pcfg);
    probe.force_state(HealthProbe::State::FAILED);

    // Create a receiver with stats showing entries received
    engine::JournalStub ctrl_stub;
    JournalReceiver::Config ctrl_rcfg;
    ctrl_rcfg.listen_port = 0;
    JournalReceiver ctrl_receiver(ctrl_rcfg, &ctrl_stub);
    auto ctrl_recv_thread = start_receiver_thread(ctrl_receiver);

    JournalReplicator::Config ctrl_scfg;
    ctrl_scfg.dr_host = "127.0.0.1";
    ctrl_scfg.dr_port = ctrl_receiver.bound_port();
    ctrl_scfg.mode = ReplicationMode::SYNC;
    JournalReplicator ctrl_replicator(ctrl_scfg);
    ASSERT_TRUE(ctrl_replicator.connect());

    recovery_journal.replay_from(1, [](const engine::JournalEntry& e, void* ctx) {
        auto* r = static_cast<JournalReplicator*>(ctx);
        r->replicate(e);
    }, &ctrl_replicator);

    ctrl_replicator.disconnect();
    ctrl_receiver.stop();
    ctrl_recv_thread.join();

    // Step 4: Start AutoPromoteController
    FailoverController failover_ctrl(&probe, &ctrl_receiver);
    GenerationCounter generation(gen_path);

    AutoPromoteConfig auto_cfg;
    auto_cfg.enabled = true;
    auto_cfg.promote_delay_ms = 100;
    auto_cfg.min_receiver_entries = 1;

    AutoPromoteController controller(auto_cfg, &probe, &failover_ctrl, &generation, &ctrl_receiver);

    // Reopen journal for promote
    recovery_journal.close();
    engine::FileJournal promote_journal;
    ASSERT_TRUE(promote_journal.open(standby_path, engine::FileJournal::FlushMode::NONE));

    engine::Partition dr_partition(1, 10000);
    std::vector<engine::InstrumentConfig> instruments = {make_config(100)};
    controller.set_promotion_target(&promote_journal, &dr_partition, instruments);

    // Track metrics
    std::mutex metrics_mtx;
    std::vector<std::string> metrics_fired;
    controller.set_metrics_callback([&](const std::string& metric) {
        std::lock_guard<std::mutex> lock(metrics_mtx);
        metrics_fired.push_back(metric);
    });

    controller.start();

    // Wait for auto-promotion
    for (int i = 0; i < 100 && controller.is_running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    controller.stop();

    // Step 5: Verify
    auto decision = controller.last_decision();
    EXPECT_TRUE(decision.promoted) << "Reason: " << decision.reason;
    EXPECT_TRUE(decision.attempted);
    EXPECT_EQ(decision.new_generation, 1u);
    EXPECT_GT(decision.entries_replayed, 0u);

    // Generation incremented
    EXPECT_EQ(generation.current(), 1u);

    // Verify DR partition state matches primary
    auto* primary_eng = primary_partition.get_engine(100);
    auto* dr_eng = dr_partition.get_engine(100);
    ASSERT_NE(primary_eng, nullptr);
    ASSERT_NE(dr_eng, nullptr);

    EXPECT_EQ(primary_eng->book().order_count(), dr_eng->book().order_count());
    EXPECT_EQ(primary_eng->book().best_bid(), dr_eng->book().best_bid());
    EXPECT_EQ(primary_eng->book().best_ask(), dr_eng->book().best_ask());

    EXPECT_TRUE(engine::StartupController::validate_state(dr_partition));

    // Verify metrics fired
    {
        std::lock_guard<std::mutex> lock(metrics_mtx);
        bool saw_fencing = false;
        bool saw_promotion = false;
        for (const auto& m : metrics_fired) {
            if (m == "failover.fencing_events") saw_fencing = true;
            if (m == "failover.promotions") saw_promotion = true;
        }
        EXPECT_TRUE(saw_fencing);
        EXPECT_TRUE(saw_promotion);
    }

    promote_journal.close();
    primary_journal.close();
    std::remove(primary_path.c_str());
    std::remove(standby_path.c_str());
    std::remove(gen_path.c_str());
}

TEST(AutoPromoteTest, StaleGenerationRejected) {
    GenerationCounter generation;

    // Simulate a promotion (increment to 1)
    uint64_t new_gen = generation.increment();
    EXPECT_EQ(new_gen, 1u);

    // Old generation 0 is now stale
    EXPECT_TRUE(generation.is_stale(0));
    EXPECT_FALSE(generation.is_stale(1));
    EXPECT_TRUE(generation.is_current(1));
}

}  // namespace
}  // namespace replication
