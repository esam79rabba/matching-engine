/// End-to-end integration test: FIX message → adapter → risk → partition → outcome
///
/// This test exercises the full FR-1 ingress path using real FIX message parsing.

#include "gateway/fix_parser.h"
#include "engine/partition.h"
#include "engine/journal_stub.h"
#include "risk/risk_service.h"

#include <gtest/gtest.h>
#include <string>

namespace {

using namespace engine;
using namespace gateway;
using namespace risk;

// ─── Test fixtures ──────────────────────────────────────────

auto sym_resolver = [](const std::string& sym) -> InstrumentId {
    if (sym == "TEST-INST") return 100;
    return 0;
};
auto price_conv = [](const std::string& val, InstrumentId) -> Price {
    return static_cast<Price>(std::stod(val));  // 1:1 for test simplicity
};
auto qty_conv = [](const std::string& val, InstrumentId) -> Quantity {
    return static_cast<Quantity>(std::stod(val));
};

InstrumentConfig make_config() {
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
    return cfg;
}

/// Helper: translate a NewOrderCommand into a PartitionMessage for submission.
PartitionMessage to_partition_msg(const NewOrderCommand& cmd) {
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::NewOrder;
    msg.timestamp = 1000000000;
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

// ═══════════════════════════════════════════════════════════════
// E2E TESTS
// ═══════════════════════════════════════════════════════════════

TEST(E2EIntegrationTest, LimitOrderAcceptedAndResting) {
    RealFixAdapter adapter(sym_resolver, price_conv, qty_conv);
    BasicRiskService risk_svc;
    JournalStub journal(10000);
    Partition partition(1, 10000, &journal);
    partition.add_instrument(make_config());

    // Step 1: Parse FIX NewOrderSingle
    std::string fix = "8=FIX.4.4|35=D|11=1001|55=TEST-INST|54=1|40=2|44=10000|38=100|59=0|";
    auto cmd = adapter.parse_new_order(fix);
    ASSERT_TRUE(cmd.has_value()) << "FIX parse failed";

    // Step 2: Risk check
    RiskRequest risk_req{};
    risk_req.client_order_id = cmd->client_order_id;
    risk_req.instrument_id = cmd->instrument_id;
    risk_req.participant_id = cmd->participant_id;
    risk_req.side = cmd->side;
    risk_req.order_type = cmd->order_type;
    risk_req.price = cmd->price;
    risk_req.quantity = cmd->quantity;
    risk_req.reference_price = 10000;
    risk_req.price_multiplier = 1;

    auto risk_resp = risk_svc.check(risk_req);
    ASSERT_EQ(risk_resp.result, RiskResult::PASS) << "Risk check failed";

    // Step 3: Submit to partition
    auto msg = to_partition_msg(*cmd);
    auto result = partition.process(msg);

    // Step 4: Verify outcome
    EXPECT_EQ(result.action, MatchResult::Action::Inserted);
    EXPECT_TRUE(result.order_resting);

    // Step 5: Verify journal recorded both input and output
    EXPECT_EQ(journal.entry_count(), 2u);

    // Step 6: Build FIX ack
    OrderId venue_id = (static_cast<uint64_t>(1) << 48) | 1;
    auto ack = adapter.build_new_ack(*cmd, venue_id);
    EXPECT_NE(ack.find("37="), std::string::npos);
    EXPECT_NE(ack.find("39=0"), std::string::npos);  // OrdStatus=New
}

TEST(E2EIntegrationTest, CrossingOrderProducesTrade) {
    RealFixAdapter adapter(sym_resolver, price_conv, qty_conv);
    BasicRiskService risk_svc;
    JournalStub journal(10000);
    Partition partition(1, 10000, &journal);
    partition.add_instrument(make_config());

    // Step 1: Resting sell @ 10050
    std::string fix_sell = "8=FIX.4.4|35=D|11=2001|55=TEST-INST|54=2|40=2|44=10050|38=100|";
    auto sell_cmd = adapter.parse_new_order(fix_sell);
    ASSERT_TRUE(sell_cmd.has_value());
    partition.process(to_partition_msg(*sell_cmd));

    // Step 2: Aggressive buy @ 10050 — should match
    std::string fix_buy = "8=FIX.4.4|35=D|11=2002|55=TEST-INST|54=1|40=2|44=10050|38=100|";
    auto buy_cmd = adapter.parse_new_order(fix_buy);
    ASSERT_TRUE(buy_cmd.has_value());

    auto risk_resp = risk_svc.check({
        buy_cmd->client_order_id, buy_cmd->instrument_id,
        buy_cmd->participant_id, buy_cmd->account_id, buy_cmd->session_id,
        buy_cmd->side, buy_cmd->order_type,
        buy_cmd->price, buy_cmd->quantity, 10000, 1
    });
    ASSERT_EQ(risk_resp.result, RiskResult::PASS);

    auto result = partition.process(to_partition_msg(*buy_cmd));

    // Verify: fully filled
    EXPECT_EQ(result.action, MatchResult::Action::Filled);

    // Verify: book is empty (both orders consumed)
    auto* eng = partition.get_engine(100);
    ASSERT_NE(eng, nullptr);
    EXPECT_EQ(eng->book().order_count(), 0u);

    // Build fill response
    auto fill = adapter.build_fill(1, 10050, 100, 100, 0, engine::Side::Buy);
    EXPECT_NE(fill.find("39=2"), std::string::npos);  // OrdStatus=Filled
}

TEST(E2EIntegrationTest, RiskRejectPreventsEngineProcessing) {
    RealFixAdapter adapter(sym_resolver, price_conv, qty_conv);
    BasicRiskService risk_svc;

    // Set tight limits
    RiskLimits tight;
    tight.max_order_qty = 50;  // Max 50
    risk_svc.set_default_limits(tight);

    JournalStub journal(10000);
    Partition partition(1, 10000, &journal);
    partition.add_instrument(make_config());

    // Parse order for qty=100 (exceeds limit)
    std::string fix = "8=FIX.4.4|35=D|11=3001|55=TEST-INST|54=1|40=2|44=10000|38=100|";
    auto cmd = adapter.parse_new_order(fix);
    ASSERT_TRUE(cmd.has_value());

    auto risk_resp = risk_svc.check({
        cmd->client_order_id, cmd->instrument_id,
        cmd->participant_id, cmd->account_id, cmd->session_id,
        cmd->side, cmd->order_type,
        cmd->price, cmd->quantity, 10000, 1
    });

    // Risk rejects — order never reaches the partition
    EXPECT_EQ(risk_resp.result, RiskResult::REJECT);
    EXPECT_EQ(risk_resp.reject_code, engine::RejectCode::QuantityExceedsMax);

    // Partition has zero entries (order was blocked)
    EXPECT_EQ(journal.entry_count(), 0u);

    // Build reject FIX message
    auto reject = adapter.build_reject(cmd->client_order_id, risk_resp.reject_code);
    EXPECT_NE(reject.find("39=8"), std::string::npos);  // OrdStatus=Rejected
}

TEST(E2EIntegrationTest, CancelFlowEndToEnd) {
    RealFixAdapter adapter(sym_resolver, price_conv, qty_conv);
    BasicRiskService risk_svc;
    JournalStub journal(10000);
    Partition partition(1, 10000, &journal);
    partition.add_instrument(make_config());

    // Insert resting buy
    std::string fix_buy = "8=FIX.4.4|35=D|11=4001|55=TEST-INST|54=1|40=2|44=10000|38=100|";
    auto buy_cmd = adapter.parse_new_order(fix_buy);
    ASSERT_TRUE(buy_cmd.has_value());
    partition.process(to_partition_msg(*buy_cmd));

    OrderId venue_id = (static_cast<uint64_t>(1) << 48) | 1;
    EXPECT_NE(partition.get_engine(100)->book().find_order(venue_id), nullptr);

    // Cancel via FIX
    std::string fix_cancel = "8=FIX.4.4|35=F|41=" + std::to_string(venue_id) + "|11=4002|55=TEST-INST|";
    auto cancel_cmd = adapter.parse_cancel(fix_cancel);
    ASSERT_TRUE(cancel_cmd.has_value());

    PartitionMessage cancel_msg{};
    cancel_msg.type = PartitionMessage::Type::Cancel;
    cancel_msg.timestamp = 2000000000;
    cancel_msg.orig_order_id = cancel_cmd->orig_order_id;
    cancel_msg.instrument_id = cancel_cmd->instrument_id;

    auto result = partition.process(cancel_msg);
    EXPECT_EQ(result.action, MatchResult::Action::Cancelled);
    EXPECT_EQ(partition.get_engine(100)->book().order_count(), 0u);

    // Build cancel ack
    auto ack = adapter.build_cancel_ack(venue_id);
    EXPECT_NE(ack.find("39=4"), std::string::npos);  // OrdStatus=Cancelled
}

}  // namespace
