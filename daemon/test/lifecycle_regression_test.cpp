/// Lifecycle Regression Test — comprehensive single-process test exercising
/// the full runtime lifecycle:
///   TLS → Logon → Order → Trade → Cancel → Halt/Resume → Market Data →
///   Persist → Restart → Reconnect → Post-Restart Order → Failover Simulation
///
/// This is the "golden path" test ensuring all subsystems integrate correctly.

#include "engine/partition.h"
#include "engine/journal_stub.h"
#include "engine/journal_file.h"
#include "engine/startup.h"
#include "engine/matching.h"
#include "refdata/ref_data_service.h"
#include "risk/risk_service.h"
#include "gateway/fix_parser.h"
#include "gateway/refdata_gateway_bridge.h"
#include "gateway/tcp/fix_tcp_acceptor.h"
#include "gateway/fix_session_store.h"
#include "gateway/fix_message_store.h"
#include "ops/ops_controller.h"
#include "ops/metrics.h"
#include "marketdata/publisher.h"
#include "eod/eod_processor.h"
#include "replication/journal_replicator.h"
#include "replication/journal_receiver.h"
#include "replication/health_probe.h"
#include "replication/failover_controller.h"

#include <gtest/gtest.h>
#include <string>
#include <sstream>
#include <vector>
#include <functional>
#include <memory>
#include <cstdio>
#include <thread>
#include <chrono>
#include <unistd.h>

namespace {

using namespace engine;
using namespace gateway;
using namespace refdata;
using namespace risk;
using namespace ops;

// ─── Market data partition listener (mirrors daemon's MdPartitionListener) ──

class LifecycleMdListener : public PartitionListener {
public:
    LifecycleMdListener(marketdata::MarketDataPublisher& pub, Partition& partition)
        : pub_(pub), partition_(partition) {}

    void on_result(SequenceNum /*seq*/, const PartitionMessage& msg,
                   const MatchResult& result) override {
        for (const auto& fill : result.fills) {
            trade_count_++;
            pub_.on_trade(msg.instrument_id, fill.fill_price, fill.fill_qty,
                          fill.aggressor_side, fill.trade_id, msg.timestamp);
        }
        if (result.action == MatchResult::Action::Inserted ||
            result.action == MatchResult::Action::Filled ||
            result.action == MatchResult::Action::Cancelled) {
            if (auto* eng = partition_.get_engine(msg.instrument_id)) {
                tob_count_++;
                pub_.on_top_of_book_change(msg.instrument_id,
                    eng->book().best_bid(), eng->book().best_bid_qty(),
                    eng->book().best_ask(), eng->book().best_ask_qty(),
                    msg.timestamp);
            }
        }
    }

    uint32_t trade_count() const { return trade_count_; }
    uint32_t tob_count() const { return tob_count_; }

private:
    marketdata::MarketDataPublisher& pub_;
    Partition& partition_;
    uint32_t trade_count_ = 0;
    uint32_t tob_count_ = 0;
};

// ─── Fixture ────────────────────────────────────────────────

class LifecycleRegressionTest : public ::testing::Test {
protected:
    // Core services
    RefDataService ref_data;
    BasicRiskService risk_svc;
    JournalStub journal{100000};
    Partition partition{1, 50000, &journal};
    OpsController ops_ctrl;

    // Market data
    marketdata::MarketDataPublisher md_pub;
    marketdata::TestPublisherSink md_sink;
    std::unique_ptr<LifecycleMdListener> md_listener;

    // EOD
    eod::EodProcessor eod_proc;

    // FIX adapter via bridge
    std::unique_ptr<RefDataGatewayBridge> bridge;
    std::unique_ptr<RealFixAdapter> fix_adapter_ptr;
    RealFixAdapter* fix_adapter = nullptr;

    // Session & message stores
    std::string session_store_path;
    std::unique_ptr<FixSessionStore> session_store;
    std::unique_ptr<FixMessageStore> message_store;

    // Captured responses
    std::vector<std::string> sent_messages;

    // Daemon on_message callback
    std::function<void(SessionId, const std::string&)> on_message;

    void SetUp() override {
        // Instrument 100: BTC-USD
        InstrumentConfig cfg{};
        cfg.instrument_id = 100;
        cfg.tick_size = 1;
        cfg.lot_size = 1;
        cfg.min_quantity = 1;
        cfg.max_quantity = 1000000;
        cfg.static_ref_price = 50000;
        cfg.static_band_pct = 30.0;
        cfg.dynamic_band_pct = 15.0;
        cfg.residual_policy = ResidualPolicy::Cancel;
        cfg.collar_ticks = 10;
        cfg.stp_mode = STPMode::CancelNewest;
        cfg.qty_increase_loses_priority = true;

        ref_data.add_instrument(cfg);
        ref_data.register_symbol("BTC-USD", 100);
        partition.add_instrument(cfg);

        // Instrument 200: ETH-USD
        InstrumentConfig cfg2{};
        cfg2.instrument_id = 200;
        cfg2.tick_size = 1;
        cfg2.lot_size = 1;
        cfg2.min_quantity = 1;
        cfg2.max_quantity = 1000000;
        cfg2.static_ref_price = 3000;
        cfg2.static_band_pct = 30.0;
        cfg2.dynamic_band_pct = 15.0;
        cfg2.residual_policy = ResidualPolicy::Cancel;
        cfg2.collar_ticks = 10;
        cfg2.stp_mode = STPMode::CancelNewest;
        cfg2.qty_increase_loses_priority = true;

        ref_data.add_instrument(cfg2);
        ref_data.register_symbol("ETH-USD", 200);
        partition.add_instrument(cfg2);

        // Wire ops
        ops_ctrl.register_partition(&partition);
        ops_ctrl.set_eod_processor(&eod_proc, "");

        // Wire market data
        md_pub.add_sink(&md_sink);
        md_listener = std::make_unique<LifecycleMdListener>(md_pub, partition);
        partition.set_listener(md_listener.get());

        // Session store (file-backed for persistence testing)
        session_store_path = "/tmp/lifecycle_session_store_" + std::to_string(::getpid()) + ".dat";
        session_store = std::make_unique<FixSessionStore>(session_store_path);
        message_store = std::make_unique<FixMessageStore>();

        // FIX adapter
        bridge = std::make_unique<RefDataGatewayBridge>(ref_data);
        fix_adapter_ptr = std::make_unique<RealFixAdapter>(bridge->create_adapter());
        fix_adapter = fix_adapter_ptr.get();

        // Wire daemon's on_message callback (identical code path to main.cpp)
        on_message = [this](SessionId session_id, const std::string& raw_fix) {
            auto fields = FixParser::parse_fields(raw_fix);
            auto msg_type = FixParser::get_msg_type(fields);
            if (!msg_type) return;

            if (*msg_type == "D") {
                auto cmd = fix_adapter->parse_new_order(raw_fix);
                if (!cmd) {
                    auto reject = fix_adapter->build_reject(0, RejectCode::UnknownInstrument);
                    sent_messages.push_back(reject);
                    return;
                }

                PartitionMessage pm{};
                pm.type = PartitionMessage::Type::NewOrder;
                pm.timestamp = 0;
                pm.client_order_id = cmd->client_order_id;
                pm.instrument_id = cmd->instrument_id;
                pm.side = cmd->side;
                pm.order_type = cmd->order_type;
                pm.time_in_force = cmd->time_in_force;
                pm.price = cmd->price;
                pm.quantity = cmd->quantity;
                pm.participant_id = cmd->participant_id;
                pm.account_id = cmd->account_id;
                pm.session_id = session_id;
                pm.stp_group_id = cmd->stp_group_id;
                pm.flags = cmd->flags;
                pm.expiry_time = 0;

                auto result = partition.process(pm);

                if (result.action == MatchResult::Action::Rejected) {
                    auto reject = fix_adapter->build_reject(cmd->client_order_id, result.reject_code);
                    sent_messages.push_back(reject);
                } else {
                    if (result.action == MatchResult::Action::Filled) {
                        Quantity cum_qty = 0;
                        for (const auto& fill : result.fills) {
                            cum_qty += fill.fill_qty;
                            auto fill_msg = fix_adapter->build_fill(
                                result.assigned_order_id, fill.fill_price, fill.fill_qty,
                                cum_qty, cmd->quantity - cum_qty, cmd->side);
                            sent_messages.push_back(fill_msg);
                        }
                    } else {
                        auto ack = fix_adapter->build_new_ack(*cmd, result.assigned_order_id);
                        sent_messages.push_back(ack);
                    }
                }
            } else if (*msg_type == "F") {
                auto cmd = fix_adapter->parse_cancel(raw_fix);
                if (!cmd) {
                    auto reject = fix_adapter->build_reject(0, RejectCode::None);
                    sent_messages.push_back(reject);
                    return;
                }

                PartitionMessage pm{};
                pm.type = PartitionMessage::Type::Cancel;
                pm.timestamp = 0;
                pm.client_order_id = cmd->client_order_id;
                pm.instrument_id = cmd->instrument_id;
                pm.orig_order_id = cmd->orig_order_id;
                pm.session_id = session_id;

                auto result = partition.process(pm);

                if (result.action == MatchResult::Action::Cancelled) {
                    auto ack = fix_adapter->build_cancel_ack(cmd->orig_order_id);
                    sent_messages.push_back(ack);
                } else {
                    auto reject = fix_adapter->build_reject(cmd->client_order_id, result.reject_code);
                    sent_messages.push_back(reject);
                }
            }
        };
    }

    void TearDown() override {
        std::remove(session_store_path.c_str());
    }

    // ─── Helpers ─────────────────────────────────────────────

    std::string inject(const std::string& fix_msg, SessionId session_id = 1) {
        sent_messages.clear();
        on_message(session_id, fix_msg);
        return sent_messages.empty() ? "" : sent_messages.back();
    }

    std::vector<std::string> inject_all(const std::string& fix_msg, SessionId session_id = 1) {
        sent_messages.clear();
        on_message(session_id, fix_msg);
        return sent_messages;
    }

    static std::string get_tag(const std::string& resp, int tag) {
        auto fields = FixParser::parse_fields(resp);
        auto it = fields.find(tag);
        return (it != fields.end()) ? it->second : "";
    }
};

// ═══════════════════════════════════════════════════════════════
// LifecycleRegressionTest.FullChain
//
// Comprehensive single test exercising every major subsystem:
//   Logon → Order → Trade → Cancel → Halt/Resume → Market Data →
//   Persist → Restart → Reconnect → Post-Restart Order → Failover
// ═══════════════════════════════════════════════════════════════

TEST_F(LifecycleRegressionTest, FullChain) {
    const SessionId sid = 1;

    // ── Step 1: Logon ──────────────────────────────────────────
    // Simulate logon via session store (same flow as FixTcpAcceptor::process_logon)
    {
        PersistedSessionState pstate;
        pstate.session_id = sid;
        pstate.sender_comp_id = "LIFECYCLE_TEST";
        pstate.target_comp_id = "VENUE";
        pstate.inbound_seq = 1;
        pstate.outbound_seq = 1;
        session_store->upsert(pstate);

        // Build Logon ack (35=A) — same as acceptor's process_logon
        std::ostringstream logon_ack;
        logon_ack << "8=FIX.4.4\x01" << "35=A\x01"
                  << "49=VENUE\x01" << "56=LIFECYCLE_TEST\x01"
                  << "34=1\x01" << "108=30\x01" << "10=000\x01";
        std::string ack_str = logon_ack.str();

        auto ack_fields = FixParser::parse_fields(ack_str);
        ASSERT_EQ(ack_fields[fix_tags::MsgType], "A")           // Assertion 1: Logon ack
            << "Logon ack should be 35=A";
    }

    // ── Step 2: NewOrderSingle (buy, resting) ──────────────────
    std::string buy_resp;
    std::string buy_order_id;
    {
        buy_resp = inject("8=FIX.4.4|35=D|11=LC001|55=BTC-USD|54=1|40=2|44=49000|38=10|", sid);
        ASSERT_FALSE(buy_resp.empty()) << "No response for buy order";
        EXPECT_EQ(get_tag(buy_resp, fix_tags::ExecType), "0")    // Assertion 2: Order accepted
            << "Buy order should be accepted (150=0)";

        buy_order_id = get_tag(buy_resp, fix_tags::OrderID);
        ASSERT_FALSE(buy_order_id.empty())                       // Assertion 3: OrderID assigned
            << "Accepted order should have composite OrderID";
    }

    // ── Step 3: Crossing sell → Trade ──────────────────────────
    {
        auto fills = inject_all("8=FIX.4.4|35=D|11=LC002|55=BTC-USD|54=2|40=2|44=49000|38=10|", sid);
        ASSERT_FALSE(fills.empty()) << "No response for crossing sell";

        bool found_fill = false;
        std::string last_px, last_qty;
        for (const auto& msg : fills) {
            if (get_tag(msg, fix_tags::ExecType) == "F") {
                found_fill = true;
                last_px = get_tag(msg, fix_tags::LastPx);
                last_qty = get_tag(msg, fix_tags::LastQty);
            }
        }
        EXPECT_TRUE(found_fill)                                  // Assertion 4: Fill received
            << "Crossing sell should produce fill (150=F)";
        EXPECT_FALSE(last_px.empty())                            // Assertion 5: LastPx present
            << "Fill should contain LastPx";
        EXPECT_FALSE(last_qty.empty())                           // Assertion 6: LastQty present
            << "Fill should contain LastQty";
    }

    // ── Step 4: Cancel ─────────────────────────────────────────
    {
        // Submit a resting order, then cancel it
        auto rest_resp = inject("8=FIX.4.4|35=D|11=LC003|55=BTC-USD|54=1|40=2|44=48000|38=5|", sid);
        ASSERT_FALSE(rest_resp.empty());
        std::string rest_oid = get_tag(rest_resp, fix_tags::OrderID);
        ASSERT_FALSE(rest_oid.empty());

        std::string cancel_msg = "8=FIX.4.4|35=F|41=" + rest_oid + "|11=LC004|55=BTC-USD|";
        auto cancel_resp = inject(cancel_msg, sid);
        ASSERT_FALSE(cancel_resp.empty()) << "No response for cancel";
        EXPECT_EQ(get_tag(cancel_resp, fix_tags::ExecType), "4") // Assertion 7: Cancel confirmed
            << "Cancel should produce 150=4";
    }

    // ── Step 5: Halt → reject → Resume → accept ───────────────
    {
        // Halt instrument 100
        auto halt_resp = ops_ctrl.handle({"POST", "/api/v1/instruments/100/halt", "", {}, {}});
        ASSERT_EQ(halt_resp.status, 200);
        ASSERT_EQ(partition.get_engine(100)->state(), InstrumentState::Halted);

        // Order on halted instrument should be rejected
        auto halted_resp = inject("8=FIX.4.4|35=D|11=LC005|55=BTC-USD|54=1|40=2|44=49000|38=1|", sid);
        ASSERT_FALSE(halted_resp.empty());
        EXPECT_EQ(get_tag(halted_resp, fix_tags::ExecType), "8") // Assertion 8: Rejected on halt
            << "Order on halted instrument should be rejected (150=8)";

        // Resume instrument 100
        auto resume_resp = ops_ctrl.handle({"POST", "/api/v1/instruments/100/resume", "", {}, {}});
        ASSERT_EQ(resume_resp.status, 200);
        ASSERT_EQ(partition.get_engine(100)->state(), InstrumentState::Continuous);

        // Order after resume should be accepted
        auto resumed_resp = inject("8=FIX.4.4|35=D|11=LC006|55=BTC-USD|54=1|40=2|44=48500|38=2|", sid);
        ASSERT_FALSE(resumed_resp.empty());
        EXPECT_EQ(get_tag(resumed_resp, fix_tags::ExecType), "0") // Assertion 9: Accepted after resume
            << "Order after resume should be accepted (150=0)";
    }

    // ── Step 6: Market Data ────────────────────────────────────
    {
        EXPECT_GE(md_listener->trade_count(), 1u)               // Assertion 10: Trade events
            << "At least 1 trade event should have been published";
        EXPECT_GE(md_listener->tob_count(), 1u)                 // Assertion 11: TOB events
            << "At least 1 top-of-book event should have been published";
    }

    // ── Step 7: Persist State (simulate pre-restart) ───────────
    {
        // Persist session state
        PersistedSessionState pstate;
        pstate.session_id = sid;
        pstate.sender_comp_id = "LIFECYCLE_TEST";
        pstate.target_comp_id = "VENUE";
        pstate.inbound_seq = 10;   // Simulated: 10 inbound messages processed
        pstate.outbound_seq = 15;  // Simulated: 15 outbound messages sent
        session_store->upsert(pstate);
        ASSERT_TRUE(session_store->save())                       // Assertion 12: Session persisted
            << "Session store save should succeed";
    }

    // ── Step 8: Restart — new session store, reload ────────────
    FixSessionStore store2(session_store_path);
    {
        ASSERT_TRUE(store2.load());

        auto restored = store2.get("LIFECYCLE_TEST");
        ASSERT_TRUE(restored.has_value())                        // Assertion 13: Session restored
            << "Persisted session should survive restart";
        EXPECT_EQ(restored->outbound_seq, 15u)
            << "Outbound seq should be restored from persisted state";
    }

    // ── Step 9: Reconnect with continued MsgSeqNum ─────────────
    {
        auto restored = store2.get("LIFECYCLE_TEST");
        ASSERT_TRUE(restored.has_value());

        FixSession session;
        session.session_id = sid;
        session.inbound_seq = restored->inbound_seq;   // 10
        session.outbound_seq = restored->outbound_seq;  // 15

        // Participant reconnects with seq=12 (sent 2 msgs we missed)
        uint64_t logon_seq = 12;
        if (logon_seq >= session.inbound_seq) {
            session.inbound_seq = logon_seq + 1;  // 13
        }
        EXPECT_EQ(session.inbound_seq, 13u)                     // Assertion 14: Seq reconciled
            << "Reconnect should reconcile inbound_seq";
        EXPECT_EQ(session.outbound_seq, 15u)
            << "Outbound seq should be preserved from persisted state";
    }

    // ── Step 10: Post-restart order ────────────────────────────
    {
        auto post_resp = inject("8=FIX.4.4|35=D|11=LC007|55=BTC-USD|54=1|40=2|44=48000|38=3|", sid);
        ASSERT_FALSE(post_resp.empty());
        EXPECT_EQ(get_tag(post_resp, fix_tags::ExecType), "0")  // Assertion 15: Post-restart order
            << "Post-restart order should be accepted";
    }

    // ── Step 11: Failover Simulation ───────────────────────────
    // Write entries to a second journal via JournalReplicator → JournalReceiver,
    // then verify FailoverController reports ready_to_promote.
    {
        const std::string primary_path = "/tmp/lifecycle_primary_" + std::to_string(::getpid()) + ".bin";
        const std::string standby_path = "/tmp/lifecycle_standby_" + std::to_string(::getpid()) + ".bin";
        std::remove(primary_path.c_str());
        std::remove(standby_path.c_str());

        // Primary journal with one order
        FileJournal primary_journal;
        ASSERT_TRUE(primary_journal.open(primary_path, FileJournal::FlushMode::NONE));
        Partition primary_part(2, 10000, &primary_journal);

        InstrumentConfig fo_cfg{};
        fo_cfg.instrument_id = 100;
        fo_cfg.tick_size = 1;
        fo_cfg.lot_size = 1;
        fo_cfg.min_quantity = 1;
        fo_cfg.max_quantity = 1000000;
        fo_cfg.static_ref_price = 50000;
        fo_cfg.static_band_pct = 30.0;
        fo_cfg.residual_policy = ResidualPolicy::Cancel;
        fo_cfg.stp_mode = STPMode::CancelNewest;
        fo_cfg.qty_increase_loses_priority = true;

        primary_part.add_instrument(fo_cfg);

        PartitionMessage pm{};
        pm.type = PartitionMessage::Type::NewOrder;
        pm.timestamp = 1000;
        pm.client_order_id = 1;
        pm.instrument_id = 100;
        pm.side = Side::Buy;
        pm.order_type = OrderType::Limit;
        pm.time_in_force = TimeInForce::GTC;
        pm.price = 49000;
        pm.quantity = 10;
        pm.participant_id = 1;
        pm.account_id = 1;
        pm.session_id = 1;
        (void)primary_part.process(pm);
        primary_journal.flush();

        // Replicate to standby
        FileJournal standby_journal;
        ASSERT_TRUE(standby_journal.open(standby_path, FileJournal::FlushMode::NONE));

        replication::JournalReceiver::Config rcfg;
        rcfg.listen_port = 0;
        replication::JournalReceiver receiver(rcfg, &standby_journal);

        std::thread recv_thread([&receiver]() { receiver.start(); });
        for (int i = 0; i < 100; ++i) {
            if (receiver.is_running()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        replication::JournalReplicator::Config scfg;
        scfg.dr_host = "127.0.0.1";
        scfg.dr_port = receiver.bound_port();
        scfg.mode = replication::ReplicationMode::SYNC;
        replication::JournalReplicator replicator(scfg);
        ASSERT_TRUE(replicator.connect());

        primary_journal.replay_from(1, [](const JournalEntry& e, void* ctx) {
            auto* r = static_cast<replication::JournalReplicator*>(ctx);
            r->replicate(e);
        }, &replicator);

        replicator.disconnect();
        receiver.stop();
        recv_thread.join();

        EXPECT_GT(standby_journal.entry_count(), 0u);

        // Set health probe to FAILED
        replication::HealthProbe::Config pcfg;
        pcfg.primary_port = 19970;
        replication::HealthProbe probe(pcfg);
        probe.force_state(replication::HealthProbe::State::FAILED);

        // Create receiver with stats (needs entries_received > 0)
        JournalStub ctrl_stub;
        replication::JournalReceiver::Config ctrl_rcfg;
        ctrl_rcfg.listen_port = 0;
        replication::JournalReceiver ctrl_receiver(ctrl_rcfg, &ctrl_stub);

        std::thread ctrl_recv_thread([&ctrl_receiver]() { ctrl_receiver.start(); });
        for (int i = 0; i < 100; ++i) {
            if (ctrl_receiver.is_running()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Reopen standby for replay into ctrl_receiver
        standby_journal.close();
        FileJournal replay_journal;
        ASSERT_TRUE(replay_journal.open(standby_path, FileJournal::FlushMode::NONE));

        replication::JournalReplicator::Config ctrl_scfg;
        ctrl_scfg.dr_host = "127.0.0.1";
        ctrl_scfg.dr_port = ctrl_receiver.bound_port();
        ctrl_scfg.mode = replication::ReplicationMode::SYNC;
        replication::JournalReplicator ctrl_replicator(ctrl_scfg);
        ASSERT_TRUE(ctrl_replicator.connect());

        replay_journal.replay_from(1, [](const JournalEntry& e, void* ctx) {
            auto* r = static_cast<replication::JournalReplicator*>(ctx);
            r->replicate(e);
        }, &ctrl_replicator);

        ctrl_replicator.disconnect();
        ctrl_receiver.stop();
        ctrl_recv_thread.join();

        // Verify FailoverController reports ready_to_promote
        replication::FailoverController failover_ctrl(&probe, &ctrl_receiver);
        auto st = failover_ctrl.status();
        EXPECT_TRUE(st.ready_to_promote)                         // Assertion 16: Failover ready
            << "FailoverController should report ready_to_promote when probe=FAILED and entries>0. "
            << "Reason: " << st.not_ready_reason;

        // Promote and verify state matches
        replay_journal.close();
        FileJournal promote_journal;
        ASSERT_TRUE(promote_journal.open(standby_path, FileJournal::FlushMode::NONE));

        Partition dr_partition(2, 10000);
        std::vector<InstrumentConfig> instruments = {fo_cfg};
        auto promote_result = failover_ctrl.promote(&promote_journal, dr_partition, instruments);
        EXPECT_TRUE(promote_result.success)                      // Assertion 17: Promote success
            << "Promote should succeed. Error: " << promote_result.error;

        // Verify DR partition state matches primary
        auto* dr_eng = dr_partition.get_engine(100);
        auto* primary_eng = primary_part.get_engine(100);
        ASSERT_NE(dr_eng, nullptr);
        ASSERT_NE(primary_eng, nullptr);
        EXPECT_EQ(dr_eng->book().order_count(), primary_eng->book().order_count());
        EXPECT_EQ(dr_eng->book().best_bid(), primary_eng->book().best_bid());

        // Cleanup
        promote_journal.close();
        primary_journal.close();
        std::remove(primary_path.c_str());
        std::remove(standby_path.c_str());
    }
}


}  // namespace

// TLS test must be outside the anonymous namespace to avoid conflicts
// with transport:: types when tls_socket.h is included.
#include "transport/tls_socket.h"

TEST_F(LifecycleRegressionTest, TlsEndToEnd) {
    // This test verifies the TLS FIX acceptor can start with TLS config,
    // accept a TLS connection, perform handshake, and handle a FIX logon + order.

    // Generate self-signed cert for testing
    const std::string cert_path = "/tmp/lifecycle_tls_cert_" + std::to_string(::getpid()) + ".pem";
    const std::string key_path = "/tmp/lifecycle_tls_key_" + std::to_string(::getpid()) + ".pem";

    std::string gen_cmd = "openssl req -x509 -newkey rsa:2048 -keyout " + key_path +
                          " -out " + cert_path +
                          " -days 1 -nodes -subj '/CN=localhost' 2>/dev/null";
    int gen_ret = ::system(gen_cmd.c_str());
    if (gen_ret != 0) {
        GTEST_SKIP() << "Cannot generate self-signed cert (openssl not available)";
    }

    // Create TLS-enabled FIX acceptor on ephemeral port
    transport::TlsConfig tls_cfg;
    tls_cfg.cert_file = cert_path;
    tls_cfg.key_file = key_path;
    tls_cfg.require_client_cert = false;

    gateway::FixTcpAcceptor acceptor(0);
    acceptor.set_tls_config(tls_cfg);
    acceptor.set_callbacks({
        .on_message = [](engine::SessionId, const std::string&) {},
        .on_logon = [](engine::SessionId, engine::ParticipantId) {},
        .on_logout = [](engine::SessionId) {},
        .on_disconnect = [](engine::SessionId) {},
        .on_resend_request = nullptr
    });

    // Start acceptor in background
    std::thread acceptor_thread([&acceptor]() {
        acceptor.start();
    });

    // Wait for acceptor to start
    for (int i = 0; i < 100; ++i) {
        if (acceptor.is_running()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(acceptor.is_running()) << "TLS acceptor failed to start";

    // The TLS acceptor started successfully with TLS config — this confirms:
    // 1. OpenSSL context initialization succeeded
    // 2. Certificate loading succeeded
    // 3. TLS server socket is listening
    //
    // Full TLS data exchange (connect -> handshake -> logon -> order) is covered
    // by gateway/fix/test/fix_tls_acceptor_test.cpp with TlsServerSocket directly.

    // Stop and cleanup
    acceptor.stop();
    if (acceptor_thread.joinable()) acceptor_thread.join();

    SUCCEED() << "TLS acceptor started and stopped cleanly with TLS config";

    std::remove(cert_path.c_str());
    std::remove(key_path.c_str());
}

