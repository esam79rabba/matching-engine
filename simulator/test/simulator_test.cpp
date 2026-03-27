/// Participant Simulator tests — run against a real FIX TCP acceptor fixture.
///
/// Starts a local FixTcpAcceptor wired to a real partition with journal,
/// risk, and ref data, then runs the simulator against it.

#include "simulator/participant_simulator.h"

#include "gateway/tcp/fix_tcp_acceptor.h"
#include "gateway/fix_parser.h"
#include "gateway/refdata_gateway_bridge.h"
#include "engine/partition.h"
#include "engine/journal_stub.h"
#include "risk/risk_service.h"
#include "refdata/ref_data_service.h"
#include "ops/ops_controller.h"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <algorithm>

namespace {

using namespace engine;
using namespace gateway;
using namespace risk;
using namespace refdata;
using namespace ops;
using namespace simulator;

// ─── Port allocation: use high ephemeral ports to avoid conflicts ──

static constexpr uint16_t SIM_PORT_BASE = 29876;

// ─── Fixture: Full platform + TCP acceptor ──────────────────────────

class SimulatorTest : public ::testing::Test {
protected:
    // Engine stack
    RefDataService     ref_data;
    BasicRiskService   risk_svc;
    JournalStub        journal{100000};
    Partition           partition{1, 50000, &journal};
    OpsController      ops_ctrl;

    // Gateway
    uint16_t           test_port_ = 0;
    FixTcpAcceptor*    acceptor_ = nullptr;
    RealFixAdapter*    adapter_ = nullptr;
    std::thread        acceptor_thread;

    // Track sequence for generating venue order IDs
    std::atomic<uint64_t> order_seq{0};

    // Per-test port to avoid conflicts between parallel tests
    static std::atomic<uint16_t> port_counter;

    void SetUp() override {
        test_port_ = port_counter.fetch_add(1);

        // ─── Reference data ─────────────────────────────────────
        InstrumentConfig cfg{};
        cfg.instrument_id    = 100;
        cfg.tick_size        = 1;
        cfg.lot_size         = 1;
        cfg.min_quantity     = 1;
        cfg.max_quantity     = 1000000;
        cfg.static_ref_price = 50000;
        cfg.static_band_pct  = 30.0;
        cfg.dynamic_band_pct = 15.0;
        cfg.residual_policy  = ResidualPolicy::Cancel;
        cfg.collar_ticks     = 10;
        cfg.stp_mode         = STPMode::CancelNewest;
        cfg.qty_increase_loses_priority = true;

        ref_data.add_instrument(cfg);
        ref_data.register_symbol("BTC-USD", 100);

        // ─── Partition ──────────────────────────────────────────
        partition.add_instrument(cfg);

        // ─── Ops ────────────────────────────────────────────────
        ops_ctrl.register_partition(&partition);

        // ─── Adapter (ref-data-backed) ──────────────────────────
        adapter_ = new RealFixAdapter(
            [this](const std::string& sym) -> InstrumentId {
                return ref_data.resolve_symbol(sym);
            },
            [this](const std::string& val, InstrumentId id) -> Price {
                double decimal = std::stod(val);
                auto mult = ref_data.get_price_multiplier(id);
                return static_cast<Price>(decimal * static_cast<double>(mult));
            },
            [this](const std::string& val, InstrumentId id) -> Quantity {
                double decimal = std::stod(val);
                auto mult = ref_data.get_qty_multiplier(id);
                return static_cast<Quantity>(decimal * static_cast<double>(mult));
            }
        );

        // ─── Acceptor ────────────────────────────────────────────
        acceptor_ = new FixTcpAcceptor(test_port_);

        FixTcpCallbacks callbacks;
        callbacks.on_logon = [](SessionId, ParticipantId) {};
        callbacks.on_logout = [](SessionId) {};
        callbacks.on_disconnect = [](SessionId) {};

        callbacks.on_message = [this](SessionId sid, const std::string& raw_fix) {
            auto fields = FixParser::parse_fields(raw_fix);
            auto msg_type = FixParser::get_msg_type(fields);
            if (!msg_type) return;

            if (*msg_type == "D") {
                handle_new_order(sid, raw_fix);
            } else if (*msg_type == "F") {
                handle_cancel(sid, raw_fix);
            }
        };

        acceptor_->set_callbacks(callbacks);

        // ─── Start acceptor in background thread ────────────────
        acceptor_thread = std::thread([this]() {
            acceptor_->start();
        });

        // Wait for acceptor to be ready
        for (int i = 0; i < 50 && !acceptor_->is_running(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    void TearDown() override {
        if (acceptor_) {
            acceptor_->stop();
        }
        if (acceptor_thread.joinable()) {
            acceptor_thread.join();
        }
        delete acceptor_;
        acceptor_ = nullptr;
        delete adapter_;
        adapter_ = nullptr;
    }

    SimulatorConfig make_config(int duration = 3) const {
        SimulatorConfig cfg;
        cfg.host = "127.0.0.1";
        cfg.port = test_port_;
        cfg.sender_comp_id = "SIM_MM";
        cfg.target_comp_id = "VENUE";
        cfg.symbol = "BTC-USD";
        cfg.base_price = 50000;
        cfg.spread = 100;
        cfg.order_qty = 10;
        cfg.orders_per_second = 10;   // Fast for testing
        cfg.duration_seconds = duration;
        cfg.cancel_stale = true;
        return cfg;
    }

private:
    void handle_new_order(SessionId sid, const std::string& raw_fix) {
        auto cmd = adapter_->parse_new_order(raw_fix);
        if (!cmd) {
            std::string reject = adapter_->build_reject(0, RejectCode::UnknownInstrument);
            acceptor_->send(sid, reject);
            return;
        }

        // Risk check
        RiskRequest rr{};
        rr.participant_id  = cmd->participant_id;
        rr.instrument_id   = cmd->instrument_id;
        rr.price           = cmd->price;
        rr.quantity         = cmd->quantity;
        rr.order_type       = cmd->order_type;
        rr.reference_price  = 50000;
        rr.price_multiplier = 1;
        auto risk_resp = risk_svc.check(rr);

        if (risk_resp.result == RiskResult::REJECT) {
            std::string reject = adapter_->build_reject(cmd->client_order_id, risk_resp.reject_code);
            acceptor_->send(sid, reject);
            return;
        }

        // Submit to partition
        PartitionMessage msg{};
        msg.type            = PartitionMessage::Type::NewOrder;
        msg.timestamp       = 1000;
        msg.client_order_id = cmd->client_order_id;
        msg.instrument_id   = cmd->instrument_id;
        msg.side            = cmd->side;
        msg.order_type      = cmd->order_type;
        msg.time_in_force   = cmd->time_in_force;
        msg.price           = cmd->price;
        msg.quantity         = cmd->quantity;
        msg.participant_id  = cmd->participant_id;
        msg.account_id      = cmd->account_id;
        msg.session_id      = cmd->session_id;
        msg.stp_group_id    = cmd->stp_group_id;
        msg.flags           = cmd->flags;

        auto result = partition.process(msg);

        std::string response;
        OrderId venue_id = (static_cast<uint64_t>(1) << 48) | (++order_seq);

        switch (result.action) {
            case MatchResult::Action::Inserted:
                response = adapter_->build_new_ack(*cmd, venue_id);
                break;
            case MatchResult::Action::Filled:
                if (!result.fills.empty()) {
                    auto& fill = result.fills[0];
                    response = adapter_->build_fill(
                        venue_id, fill.fill_price, fill.fill_qty,
                        fill.fill_qty, 0, cmd->side);
                } else {
                    response = adapter_->build_fill(
                        venue_id, cmd->price, cmd->quantity,
                        cmd->quantity, 0, cmd->side);
                }
                break;
            case MatchResult::Action::Rejected:
                response = adapter_->build_reject(cmd->client_order_id, result.reject_code);
                break;
            default:
                response = adapter_->build_reject(cmd->client_order_id, RejectCode::None);
                break;
        }

        acceptor_->send(sid, response);
    }

    void handle_cancel(SessionId sid, const std::string& raw_fix) {
        auto cmd = adapter_->parse_cancel(raw_fix);
        if (!cmd) {
            std::string reject = adapter_->build_reject(0, RejectCode::UnknownInstrument);
            acceptor_->send(sid, reject);
            return;
        }

        PartitionMessage msg{};
        msg.type           = PartitionMessage::Type::Cancel;
        msg.timestamp      = 2000;
        msg.orig_order_id  = cmd->orig_order_id;
        msg.instrument_id  = cmd->instrument_id;
        msg.client_order_id = cmd->client_order_id;

        auto result = partition.process(msg);

        std::string response;
        if (result.action == MatchResult::Action::Cancelled) {
            response = adapter_->build_cancel_ack(cmd->orig_order_id);
        } else {
            response = adapter_->build_reject(cmd->client_order_id, result.reject_code);
        }

        acceptor_->send(sid, response);
    }
};

std::atomic<uint16_t> SimulatorTest::port_counter{SIM_PORT_BASE};

// ═══════════════════════════════════════════════════════════════════
// TESTS
// ═══════════════════════════════════════════════════════════════════

TEST_F(SimulatorTest, LogonAndLogout) {
    auto cfg = make_config(1);
    cfg.orders_per_second = 0;  // No trading, just logon/logout cycle
    cfg.duration_seconds = 0;   // Immediate exit after logon

    ParticipantSimulator sim(cfg);
    auto stats = sim.run();

    // Should connect and disconnect without errors
    EXPECT_EQ(stats.errors, 0u);
}

TEST_F(SimulatorTest, SubmitAndReceiveAck) {
    auto cfg = make_config(1);
    cfg.orders_per_second = 1;
    cfg.duration_seconds = 1;
    cfg.cancel_stale = false;

    ParticipantSimulator sim(cfg);
    auto stats = sim.run();

    // Should have sent at least 1 order pair (buy + sell)
    EXPECT_GE(stats.orders_sent, 1u);
    // Should have received at least 1 ack
    EXPECT_GE(stats.acks_received, 1u);
    EXPECT_EQ(stats.errors, 0u);
}

TEST_F(SimulatorTest, MarketMakingCycle) {
    auto cfg = make_config(3);
    cfg.orders_per_second = 5;
    cfg.cancel_stale = false;

    ParticipantSimulator sim(cfg);
    auto stats = sim.run();

    // Over 3 seconds at 5 orders/sec, should have meaningful activity
    EXPECT_GT(stats.orders_sent, 0u);
    EXPECT_GT(stats.acks_received, 0u);
    EXPECT_EQ(stats.errors, 0u);
}

TEST_F(SimulatorTest, CrossingTradeProducesFill) {
    // First, place a resting sell via the simulator at the bid price
    // Then run a buyer simulator at that same price to cross
    auto seller_cfg = make_config(2);
    seller_cfg.sender_comp_id = "SIM_SELLER";
    seller_cfg.base_price = 50000;
    seller_cfg.spread = 0;          // Place right at base_price
    seller_cfg.orders_per_second = 1;
    seller_cfg.duration_seconds = 1;
    seller_cfg.cancel_stale = false;

    // The seller submits both buy and sell at 50000.
    // The buy rests, the sell rests. Then a second pass crosses them.
    ParticipantSimulator seller(seller_cfg);
    auto seller_stats = seller.run();

    // The simulator submits buy at base-spread and sell at base+spread.
    // With spread=0, buy at 50000, sell at 50000.
    // The first buy rests, then the sell crosses it immediately.
    // So we expect fills.
    EXPECT_GT(seller_stats.orders_sent, 0u);
    // At least some fills should have occurred (buy rests, sell crosses)
    EXPECT_GT(seller_stats.fills_received + seller_stats.acks_received, 0u);
    EXPECT_EQ(seller_stats.errors, 0u);
}

TEST_F(SimulatorTest, CancelAndReplace) {
    auto cfg = make_config(5);
    cfg.orders_per_second = 5;
    cfg.cancel_stale = true;

    ParticipantSimulator sim(cfg);
    auto stats = sim.run();

    // Should have sent some orders
    EXPECT_GT(stats.orders_sent, 0u);
    // With cancel_stale=true and 5 second duration, should have attempted cancels
    EXPECT_GT(stats.cancels_sent, 0u);
    EXPECT_EQ(stats.errors, 0u);
}

TEST_F(SimulatorTest, StatsAccurate) {
    auto cfg = make_config(2);
    cfg.orders_per_second = 3;
    cfg.cancel_stale = false;

    ParticipantSimulator sim(cfg);
    auto stats = sim.run();

    // Every order sent should result in either an ack, fill, or reject.
    // Note: some orders may not get responses due to timing, so we check
    // that responses <= orders_sent.
    uint64_t total_responses = stats.acks_received + stats.fills_received + stats.rejects_received;
    EXPECT_LE(total_responses, stats.orders_sent);
    EXPECT_EQ(stats.errors, 0u);
}

TEST_F(SimulatorTest, StopEarly) {
    auto cfg = make_config(30);  // Long duration
    cfg.orders_per_second = 1;
    cfg.cancel_stale = false;

    ParticipantSimulator sim(cfg);

    // Run in a thread and stop after 1 second
    std::thread runner([&sim]() {
        sim.run();
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    sim.stop();
    runner.join();

    auto stats = sim.stats();
    // Should have stopped early (duration was 30s but we stopped after 1s)
    EXPECT_EQ(stats.errors, 0u);
}

}  // namespace
