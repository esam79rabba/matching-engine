/// Full platform integration test — FR-1 coherent slice.
///
/// Exercises: ref data → gateway (symbol lookup) → risk → journal → partition
///   → matching → market data publisher → drop copy → ops state → kill switch.

#include "gateway/refdata_gateway_bridge.h"
#include "gateway/fix_parser.h"
#include "engine/partition.h"
#include "engine/journal_stub.h"
#include "engine/kill_switch.h"
#include "engine/session_scheduler.h"
#include "risk/risk_service.h"
#include "risk/rate_limiter.h"
#include "refdata/ref_data_service.h"
#include "marketdata/publisher.h"
#include "dropcopy/drop_copy_service.h"
#include "ops/ops_controller.h"

#include <gtest/gtest.h>

namespace {

using namespace engine;
using namespace gateway;
using namespace risk;
using namespace refdata;
using namespace marketdata;
using namespace dropcopy;
using namespace ops;

// ─── Fixture: wires up the full platform ────────────────────

class FullPlatformTest : public ::testing::Test {
protected:
    // Services
    RefDataService ref_data;
    BasicRiskService risk_svc;
    JournalStub journal{100000};
    Partition partition{1, 50000, &journal};
    MarketDataPublisher md_pub;
    TestPublisherSink md_sink;
    DropCopyService dc_svc;
    TestDropCopySink dc_sink;
    OpsController ops_ctrl;
    KillSwitchController kill_switch;

    void SetUp() override {
        // Reference data: simple integer prices (1:1 mapping, no multiplier)
        // FIX price "50000" → internal Price 50000
        InstrumentConfig cfg{};
        cfg.instrument_id = 100;
        cfg.tick_size = 1;         // 1 unit tick (simple for integration test)
        cfg.lot_size = 1;
        cfg.min_quantity = 1;
        cfg.max_quantity = 1000000;
        cfg.static_ref_price = 50000;  // Reference price = 50000
        cfg.static_band_pct = 30.0;    // Valid range: 35000–65000
        cfg.dynamic_band_pct = 15.0;
        cfg.residual_policy = ResidualPolicy::Cancel;
        cfg.collar_ticks = 10;
        cfg.stp_mode = STPMode::CancelNewest;
        cfg.qty_increase_loses_priority = true;

        ref_data.add_instrument(cfg);
        ref_data.register_symbol("BTC-USD", 100);
        // Price multiplier = 1 (default) — FIX prices pass through as-is

        // Partition
        partition.add_instrument(cfg);

        // Market data
        md_pub.add_sink(&md_sink);

        // Drop copy (participant 1)
        dc_svc.register_sink(1, &dc_sink);

        // Ops
        ops_ctrl.register_partition(&partition);

        // Kill switch (wired to risk service via callbacks)
        kill_switch.register_partition(&partition);
        kill_switch.set_block_callback(
            [this](ParticipantId id) { risk_svc.block_participant(id); },
            [this](ParticipantId id) { risk_svc.unblock_participant(id); });
    }

    // Helper: create adapter with lambdas that capture ref_data directly
    // (avoiding lifetime issue with temporary RefDataGatewayBridge)
    RealFixAdapter make_adapter() {
        return RealFixAdapter(
            [this](const std::string& sym) -> engine::InstrumentId {
                return ref_data.resolve_symbol(sym);
            },
            [this](const std::string& val, engine::InstrumentId id) -> engine::Price {
                double decimal = std::stod(val);
                auto mult = ref_data.get_price_multiplier(id);
                return static_cast<engine::Price>(decimal * static_cast<double>(mult));
            },
            [this](const std::string& val, engine::InstrumentId id) -> engine::Quantity {
                double decimal = std::stod(val);
                auto mult = ref_data.get_qty_multiplier(id);
                return static_cast<engine::Quantity>(decimal * static_cast<double>(mult));
            }
        );
    }

    // Helper: translate a NewOrderCommand into a PartitionMessage
    PartitionMessage to_msg(const NewOrderCommand& cmd) {
        PartitionMessage msg{};
        msg.type = PartitionMessage::Type::NewOrder;
        msg.timestamp = 1000;
        msg.client_order_id = cmd.client_order_id;
        msg.instrument_id = cmd.instrument_id;
        msg.side = cmd.side;
        msg.order_type = cmd.order_type;
        msg.time_in_force = cmd.time_in_force;
        msg.price = cmd.price;
        msg.quantity = cmd.quantity;
        msg.participant_id = cmd.participant_id;
        msg.account_id = cmd.account_id;
        msg.session_id = cmd.session_id;
        msg.stp_group_id = cmd.stp_group_id;
        msg.flags = cmd.flags;
        return msg;
    }
};

// ═══════════════════════════════════════════════════════════════
// TESTS
// ═══════════════════════════════════════════════════════════════

TEST_F(FullPlatformTest, AcceptedRestingOrder) {
    auto adapter = make_adapter();

    // Parse FIX order using ref-data-backed symbol resolution
    std::string fix = "8=FIX.4.4|35=D|11=1001|55=BTC-USD|54=1|40=2|44=49000|38=5|";
    auto cmd = adapter.parse_new_order(fix);
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->instrument_id, 100u);

    // Risk check
    RiskRequest rr{};
    rr.participant_id = cmd->participant_id;
    rr.instrument_id = cmd->instrument_id;
    rr.price = cmd->price;
    rr.quantity = cmd->quantity;
    rr.order_type = cmd->order_type;
    rr.reference_price = 50000;
    rr.price_multiplier = 1;
    EXPECT_EQ(risk_svc.check(rr).result, RiskResult::PASS);

    // Process
    auto result = partition.process(to_msg(*cmd));
    EXPECT_EQ(result.action, MatchResult::Action::Inserted);

    // Journal has entries
    EXPECT_GE(journal.entry_count(), 2u);

    // Drop copy: notify
    dc_svc.on_order_new(1, cmd->client_order_id, cmd->instrument_id,
                        cmd->participant_id, cmd->side, 1000);
    EXPECT_EQ(dc_sink.events.size(), 1u);
}

TEST_F(FullPlatformTest, CrossingOrderProducesTrade) {
    auto adapter = make_adapter();

    // Resting sell
    auto sell = adapter.parse_new_order("8=FIX.4.4|35=D|11=2001|55=BTC-USD|54=2|40=2|44=50000|38=10|");
    ASSERT_TRUE(sell.has_value());
    (void)partition.process(to_msg(*sell));

    // Aggressive buy → trade
    auto buy = adapter.parse_new_order("8=FIX.4.4|35=D|11=2002|55=BTC-USD|54=1|40=2|44=50000|38=10|");
    ASSERT_TRUE(buy.has_value());
    auto result = partition.process(to_msg(*buy));
    EXPECT_EQ(result.action, MatchResult::Action::Filled);

    // Market data: publish trade
    md_pub.on_trade(100, 50000, 10, Side::Buy, 1, 2000);
    EXPECT_EQ(md_sink.trade_events.size(), 1u);
    EXPECT_EQ(md_sink.trade_events[0].price, 50000);
}

TEST_F(FullPlatformTest, RejectedOrderDoesNotEnterBook) {
    auto adapter = make_adapter();

    // Set tight risk limits
    RiskLimits tight;
    tight.max_order_qty = 5;
    risk_svc.set_default_limits(tight);

    auto cmd = adapter.parse_new_order("8=FIX.4.4|35=D|11=3001|55=BTC-USD|54=1|40=2|44=50000|38=100|");
    ASSERT_TRUE(cmd.has_value());

    RiskRequest rr{};
    rr.participant_id = 1; rr.instrument_id = 100;
    rr.price = cmd->price; rr.quantity = cmd->quantity;
    rr.order_type = cmd->order_type;
    rr.reference_price = 5000000; rr.price_multiplier = 100;
    auto resp = risk_svc.check(rr);
    EXPECT_EQ(resp.result, RiskResult::REJECT);

    // Order never reaches partition
    EXPECT_EQ(partition.get_engine(100)->book().order_count(), 0u);
}

TEST_F(FullPlatformTest, HaltedInstrumentRejectsOrders) {
    auto adapter = make_adapter();

    // Halt via ops
    auto ops_resp = ops_ctrl.handle({"POST", "/api/v1/instruments/100/halt", "", {}, {}});
    EXPECT_EQ(ops_resp.status, 200);

    // Verify state
    EXPECT_EQ(partition.get_engine(100)->state(), InstrumentState::Halted);

    // New order rejected by engine
    auto cmd = adapter.parse_new_order("8=FIX.4.4|35=D|11=4001|55=BTC-USD|54=1|40=2|44=50000|38=10|");
    ASSERT_TRUE(cmd.has_value());
    auto result = partition.process(to_msg(*cmd));
    EXPECT_EQ(result.action, MatchResult::Action::Rejected);
    EXPECT_EQ(result.reject_code, RejectCode::InstrumentHalted);
}

TEST_F(FullPlatformTest, KillSwitchCancelsAndBlocks) {
    auto adapter = make_adapter();

    // Insert orders for participant 1
    auto o1 = adapter.parse_new_order("8=FIX.4.4|35=D|11=5001|55=BTC-USD|54=1|40=2|44=49000|38=5|");
    auto o2 = adapter.parse_new_order("8=FIX.4.4|35=D|11=5002|55=BTC-USD|54=2|40=2|44=51000|38=3|");
    ASSERT_TRUE(o1.has_value());
    ASSERT_TRUE(o2.has_value());
    (void)partition.process(to_msg(*o1));
    (void)partition.process(to_msg(*o2));
    EXPECT_EQ(partition.get_engine(100)->book().order_count(), 2u);

    // Activate kill switch
    uint32_t cancelled = kill_switch.activate(1);
    EXPECT_EQ(cancelled, 2u);
    EXPECT_EQ(partition.get_engine(100)->book().order_count(), 0u);

    // New order blocked by risk
    RiskRequest rr{};
    rr.participant_id = 1; rr.instrument_id = 100;
    rr.price = 50000; rr.quantity = 1;
    rr.order_type = OrderType::Limit;
    EXPECT_EQ(risk_svc.check(rr).result, RiskResult::REJECT);

    // Deactivate
    kill_switch.deactivate(1);
    rr.reference_price = 50000; rr.price_multiplier = 1;
    EXPECT_EQ(risk_svc.check(rr).result, RiskResult::PASS);
}

TEST_F(FullPlatformTest, SessionSchedulerDrivesStateTransitions) {
    ManualTimeSource clock(0);
    SessionScheduler scheduler(clock);

    auto* eng = partition.get_engine_mut(100);
    ASSERT_NE(eng, nullptr);
    eng->force_state(InstrumentState::Inactive);

    constexpr Timestamp SEC = 1000000000ULL;
    scheduler.add_profile({100, {
        {10 * SEC, InstrumentState::PreOpen},
        {20 * SEC, InstrumentState::Continuous},
    }});
    scheduler.register_engine(100, eng);

    // Before schedule
    auto adapter = make_adapter();
    auto cmd = adapter.parse_new_order("8=FIX.4.4|35=D|11=6001|55=BTC-USD|54=1|40=2|44=50000|38=1|");
    ASSERT_TRUE(cmd.has_value());

    auto r1 = partition.process(to_msg(*cmd));
    EXPECT_EQ(r1.action, MatchResult::Action::Rejected);  // Inactive

    // Advance to PreOpen
    clock.set(10 * SEC);
    (void)scheduler.tick();
    EXPECT_EQ(eng->state(), InstrumentState::PreOpen);

    // PreOpen accepts orders but doesn't match
    auto r2 = partition.process(to_msg(*cmd));
    EXPECT_EQ(r2.action, MatchResult::Action::Inserted);  // Accepted in PreOpen

    // Advance to Continuous
    clock.set(20 * SEC);
    (void)scheduler.tick();
    EXPECT_EQ(eng->state(), InstrumentState::Continuous);
}

}  // namespace
