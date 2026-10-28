/// Tests for extended FIX message types: H, q, 8(I), r, AE, j, W
///
/// Validates parse and build methods added in the "missing FIX message types" pass.

#include "gateway/fix_parser.h"
#include "gateway/fix_gateway_adapter.h"
#include <gtest/gtest.h>
#include <string>

namespace {

// ─── Test fixture with a pre-wired RealFixAdapter ────────────────

class FixMessageTypesTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto symbol_resolver = [](const std::string& sym) -> engine::InstrumentId {
            if (sym == "AAPL" || sym == "100") return 100;
            if (sym == "MSFT" || sym == "200") return 200;
            return 0;
        };
        auto price_converter = [](const std::string& p, engine::InstrumentId) -> engine::Price {
            return static_cast<engine::Price>(std::stoll(p));
        };
        auto qty_converter = [](const std::string& q, engine::InstrumentId) -> engine::Quantity {
            return static_cast<engine::Quantity>(std::stoull(q));
        };
        auto participant_resolver = [](const std::string& comp_id) -> engine::ParticipantId {
            if (comp_id == "FIRM_A") return 1;
            return 0;
        };

        adapter = std::make_unique<gateway::RealFixAdapter>(
            symbol_resolver, price_converter, qty_converter, participant_resolver);
    }

    std::unique_ptr<gateway::RealFixAdapter> adapter;

    // Helper to build a SOH-delimited FIX message from pipe-delimited input
    static std::string soh(const std::string& pipe_msg) {
        std::string msg = pipe_msg;
        for (auto& c : msg) {
            if (c == '|') c = '\x01';
        }
        return msg;
    }

    // Helper to extract a tag value from a SOH-delimited FIX message
    static std::string tag_value(const std::string& msg, int tag) {
        auto fields = gateway::FixParser::parse_fields(msg);
        auto it = fields.find(tag);
        return (it != fields.end()) ? it->second : "";
    }
};

// ═══════════════════════════════════════════════════════════════
// 1. ParseOrderStatusRequest — extract order ID from H message
// ═══════════════════════════════════════════════════════════════

TEST_F(FixMessageTypesTest, ParseOrderStatusRequest) {
    auto msg = soh("8=FIX.4.4|35=H|37=12345|11=ORD001|55=AAPL|");
    auto cmd = adapter->parse_order_status_request(msg);

    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->order_id, 12345u);
    EXPECT_EQ(cmd->raw_client_order_id, "ORD001");
    EXPECT_EQ(cmd->instrument_id, 100u);
}

TEST_F(FixMessageTypesTest, ParseOrderStatusRequest_ClOrdIDOnly) {
    auto msg = soh("8=FIX.4.4|35=H|11=5555|55=MSFT|");
    auto cmd = adapter->parse_order_status_request(msg);

    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->client_order_id, 5555u);
    EXPECT_EQ(cmd->instrument_id, 200u);
}

TEST_F(FixMessageTypesTest, ParseOrderStatusRequest_MissingIdentifiers) {
    auto msg = soh("8=FIX.4.4|35=H|55=AAPL|");
    auto cmd = adapter->parse_order_status_request(msg);

    EXPECT_FALSE(cmd.has_value());
}

// ═══════════════════════════════════════════════════════════════
// 2. ParseMassCancelByInstrument — parse q message with scope=1
// ═══════════════════════════════════════════════════════════════

TEST_F(FixMessageTypesTest, ParseMassCancelByInstrument) {
    auto msg = soh("8=FIX.4.4|35=q|530=1|55=AAPL|11=MC001|49=FIRM_A|");
    auto cmd = adapter->parse_mass_cancel_request(msg);

    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(static_cast<uint8_t>(cmd->scope),
              static_cast<uint8_t>(gateway::MassCancelCommand::Scope::ByInstrument));
    EXPECT_EQ(cmd->instrument_id, 100u);
    EXPECT_EQ(cmd->raw_client_order_id, "MC001");
    EXPECT_EQ(cmd->participant_id, 1u);
}

TEST_F(FixMessageTypesTest, ParseMassCancelAll) {
    auto msg = soh("8=FIX.4.4|35=q|530=7|11=MC002|");
    auto cmd = adapter->parse_mass_cancel_request(msg);

    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(static_cast<uint8_t>(cmd->scope),
              static_cast<uint8_t>(gateway::MassCancelCommand::Scope::All));
}

TEST_F(FixMessageTypesTest, ParseMassCancelMissingType) {
    auto msg = soh("8=FIX.4.4|35=q|55=AAPL|");
    auto cmd = adapter->parse_mass_cancel_request(msg);

    EXPECT_FALSE(cmd.has_value());
}

// ═══════════════════════════════════════════════════════════════
// 3. BuildOrderStatusResponse — verify MsgType=8, ExecType=I tags
// ═══════════════════════════════════════════════════════════════

TEST_F(FixMessageTypesTest, BuildOrderStatusResponse) {
    auto resp = adapter->build_order_status_response(
        12345, engine::Side::Buy, 15000, 50, 100, "0");

    EXPECT_EQ(tag_value(resp, 35), "8");     // MsgType = ExecutionReport
    EXPECT_EQ(tag_value(resp, 150), "I");    // ExecType = OrderStatus
    EXPECT_EQ(tag_value(resp, 39), "0");     // OrdStatus = New
    EXPECT_EQ(tag_value(resp, 37), "12345"); // OrderID
    EXPECT_EQ(tag_value(resp, 54), "1");     // Side = Buy
    EXPECT_EQ(tag_value(resp, 44), "15000"); // Price
    EXPECT_EQ(tag_value(resp, 14), "100");   // CumQty
    EXPECT_EQ(tag_value(resp, 151), "50");   // LeavesQty
}

// ═══════════════════════════════════════════════════════════════
// 4. BuildMassCancelReport — verify MsgType=r tags
// ═══════════════════════════════════════════════════════════════

TEST_F(FixMessageTypesTest, BuildMassCancelReport) {
    auto report = adapter->build_mass_cancel_report(9001, 42);

    EXPECT_EQ(tag_value(report, 35), "r");    // MsgType = OrderMassCancelReport
    EXPECT_EQ(tag_value(report, 11), "9001"); // ClOrdID
    EXPECT_EQ(tag_value(report, 531), "1");   // MassCancelResponse: accepted
    EXPECT_EQ(tag_value(report, 533), "42");  // TotalAffectedOrders
}

// ═══════════════════════════════════════════════════════════════
// 5. BuildTradeCaptureReport — verify MsgType=AE tags
// ═══════════════════════════════════════════════════════════════

TEST_F(FixMessageTypesTest, BuildTradeCaptureReport) {
    auto report = adapter->build_trade_capture_report(
        7777, 1001, 2002, 15000, 200, 100);

    EXPECT_EQ(tag_value(report, 35), "AE");    // MsgType = TradeCaptureReport
    EXPECT_EQ(tag_value(report, 571), "7777"); // TradeReportID
    EXPECT_EQ(tag_value(report, 55), "100");   // Symbol (instrument_id)
    EXPECT_EQ(tag_value(report, 32), "200");   // LastQty
    EXPECT_EQ(tag_value(report, 31), "15000"); // LastPx

    // Verify NoSides group present
    EXPECT_EQ(tag_value(report, 552), "2");    // NoSides = 2
}

// ═══════════════════════════════════════════════════════════════
// 6. BuildBusinessReject — verify MsgType=j with reason
// ═══════════════════════════════════════════════════════════════

TEST_F(FixMessageTypesTest, BuildBusinessReject) {
    auto reject = adapter->build_business_reject("Z", "Unsupported message type");

    EXPECT_EQ(tag_value(reject, 35), "j");    // MsgType = BusinessMessageReject
    EXPECT_EQ(tag_value(reject, 372), "Z");   // RefMsgType
    EXPECT_EQ(tag_value(reject, 380), "3");   // BusinessRejectReason: Unsupported msg type
    EXPECT_EQ(tag_value(reject, 58), "Unsupported message type");  // Text
}

// ═══════════════════════════════════════════════════════════════
// 7. BuildMarketDataSnapshot — verify MsgType=W with bid/ask entries
// ═══════════════════════════════════════════════════════════════

TEST_F(FixMessageTypesTest, BuildMarketDataSnapshot) {
    std::vector<std::pair<engine::Price, engine::Quantity>> bids = {
        {15000, 100}, {14900, 200}
    };
    std::vector<std::pair<engine::Price, engine::Quantity>> asks = {
        {15100, 150}
    };

    auto snapshot = adapter->build_market_data_snapshot(100, bids, asks);

    EXPECT_EQ(tag_value(snapshot, 35), "W");    // MsgType = MarketDataSnapshotFullRefresh
    EXPECT_EQ(tag_value(snapshot, 55), "100");  // Symbol (instrument_id)
    EXPECT_EQ(tag_value(snapshot, 268), "3");   // NoMDEntries = 2 bids + 1 ask

    // Verify the message contains bid and ask entry types
    auto fields_raw = snapshot;
    // The repeating group should contain MDEntryType 0 (bid) and 1 (offer)
    EXPECT_NE(snapshot.find("269=0"), std::string::npos);  // Has bid entries
    EXPECT_NE(snapshot.find("269=1"), std::string::npos);  // Has ask entries
    EXPECT_NE(snapshot.find("270=15000"), std::string::npos);  // First bid price
    EXPECT_NE(snapshot.find("271=100"), std::string::npos);    // First bid size
    EXPECT_NE(snapshot.find("270=15100"), std::string::npos);  // Ask price
}

// ═══════════════════════════════════════════════════════════════
// 8. UnknownMsgTypeGetsBusinessReject — verify j response
// ═══════════════════════════════════════════════════════════════

TEST_F(FixMessageTypesTest, UnknownMsgTypeGetsBusinessReject) {
    // Build a reject for an unknown message type "Z"
    auto reject = adapter->build_business_reject("Z", "Unsupported message type");

    // Verify it's a valid business reject
    EXPECT_EQ(tag_value(reject, 35), "j");
    EXPECT_EQ(tag_value(reject, 372), "Z");
    EXPECT_EQ(tag_value(reject, 380), "3");

    // Verify it's properly SOH-delimited
    EXPECT_NE(reject.find('\x01'), std::string::npos);
    EXPECT_EQ(reject.substr(0, 10), "8=FIX.4.4\x01");
}

}  // namespace
