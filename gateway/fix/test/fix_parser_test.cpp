#include "gateway/fix_parser.h"
#include <gtest/gtest.h>

namespace gateway {
namespace {

// ─── FixParser Tests ────────────────────────────────────────

TEST(FixParserTest, ParsesPipeDelimitedMessage) {
    auto fields = FixParser::parse_fields("8=FIX.4.4|35=D|11=12345|55=BTC-USD|54=1|40=2|44=50000|38=100|");
    EXPECT_EQ(fields[8], "FIX.4.4");
    EXPECT_EQ(fields[35], "D");
    EXPECT_EQ(fields[11], "12345");
    EXPECT_EQ(fields[55], "BTC-USD");
    EXPECT_EQ(fields[54], "1");
    EXPECT_EQ(fields[40], "2");
    EXPECT_EQ(fields[44], "50000");
    EXPECT_EQ(fields[38], "100");
}

TEST(FixParserTest, ParsesSOHDelimitedMessage) {
    auto fields = FixParser::parse_fields("8=FIX.4.4\x01""35=D\x01""11=999\x01");
    EXPECT_EQ(fields[35], "D");
    EXPECT_EQ(fields[11], "999");
}

TEST(FixParserTest, GetMsgType) {
    auto fields = FixParser::parse_fields("8=FIX.4.4|35=D|");
    auto mt = FixParser::get_msg_type(fields);
    ASSERT_TRUE(mt.has_value());
    EXPECT_EQ(*mt, "D");
}

TEST(FixParserTest, EmptyMessageReturnsEmptyMap) {
    auto fields = FixParser::parse_fields("");
    EXPECT_TRUE(fields.empty());
}

// ─── RealFixAdapter Tests ───────────────────────────────────

// Simple test fixtures
auto test_symbol_resolver = [](const std::string& sym) -> engine::InstrumentId {
    if (sym == "BTC-USD") return 100;
    if (sym == "ETH-USD") return 101;
    return 0;
};

auto test_price_converter = [](const std::string& val, engine::InstrumentId) -> engine::Price {
    return static_cast<engine::Price>(std::stod(val) * 100);  // multiplier = 100
};

auto test_qty_converter = [](const std::string& val, engine::InstrumentId) -> engine::Quantity {
    return static_cast<engine::Quantity>(std::stod(val));
};

TEST(RealFixAdapterTest, ParsesNewOrderSingle) {
    RealFixAdapter adapter(test_symbol_resolver, test_price_converter, test_qty_converter);

    std::string fix = "8=FIX.4.4|35=D|11=1001|55=BTC-USD|54=1|40=2|44=50000.00|38=10|59=0|";
    auto cmd = adapter.parse_new_order(fix);

    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->client_order_id, 1001u);
    EXPECT_EQ(cmd->instrument_id, 100u);
    EXPECT_EQ(cmd->side, engine::Side::Buy);
    EXPECT_EQ(cmd->order_type, engine::OrderType::Limit);
    EXPECT_EQ(cmd->price, 5000000);  // 50000.00 * 100
    EXPECT_EQ(cmd->quantity, 10u);
    EXPECT_EQ(cmd->time_in_force, engine::TimeInForce::Day);
}

TEST(RealFixAdapterTest, ParsesMarketOrder) {
    RealFixAdapter adapter(test_symbol_resolver, test_price_converter, test_qty_converter);

    std::string fix = "8=FIX.4.4|35=D|11=2001|55=BTC-USD|54=2|40=1|38=5|";
    auto cmd = adapter.parse_new_order(fix);

    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->order_type, engine::OrderType::Market);
    EXPECT_EQ(cmd->side, engine::Side::Sell);
    EXPECT_EQ(cmd->price, 0);  // Market order has no price
}

TEST(RealFixAdapterTest, RejectsUnknownSymbol) {
    RealFixAdapter adapter(test_symbol_resolver, test_price_converter, test_qty_converter);

    std::string fix = "8=FIX.4.4|35=D|11=3001|55=UNKNOWN|54=1|40=2|44=100|38=10|";
    auto cmd = adapter.parse_new_order(fix);

    EXPECT_FALSE(cmd.has_value());
}

TEST(RealFixAdapterTest, RejectsMissingRequiredFields) {
    RealFixAdapter adapter(test_symbol_resolver, test_price_converter, test_qty_converter);

    // Missing Symbol (tag 55)
    std::string fix = "8=FIX.4.4|35=D|11=4001|54=1|40=2|44=100|38=10|";
    EXPECT_FALSE(adapter.parse_new_order(fix).has_value());

    // Missing Side (tag 54)
    fix = "8=FIX.4.4|35=D|11=4002|55=BTC-USD|40=2|44=100|38=10|";
    EXPECT_FALSE(adapter.parse_new_order(fix).has_value());
}

TEST(RealFixAdapterTest, ParsesCancelRequest) {
    RealFixAdapter adapter(test_symbol_resolver, test_price_converter, test_qty_converter);

    std::string fix = "8=FIX.4.4|35=F|41=99999|11=5001|55=BTC-USD|54=1|";
    auto cmd = adapter.parse_cancel(fix);

    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->orig_order_id, 99999u);
    EXPECT_EQ(cmd->client_order_id, 5001u);
    EXPECT_EQ(cmd->instrument_id, 100u);
}

TEST(RealFixAdapterTest, ParsesModifyRequest) {
    RealFixAdapter adapter(test_symbol_resolver, test_price_converter, test_qty_converter);

    std::string fix = "8=FIX.4.4|35=G|41=88888|11=6001|55=BTC-USD|44=51000.00|38=20|";
    auto cmd = adapter.parse_modify(fix);

    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->orig_order_id, 88888u);
    EXPECT_EQ(cmd->new_price, 5100000);  // 51000.00 * 100
    EXPECT_EQ(cmd->new_quantity, 20u);
}

TEST(RealFixAdapterTest, BuildsNewAckWithSOHDelimiter) {
    RealFixAdapter adapter(test_symbol_resolver, test_price_converter, test_qty_converter);

    NewOrderCommand cmd{};
    cmd.client_order_id = 7777;
    cmd.instrument_id = 100;
    cmd.side = engine::Side::Buy;
    cmd.price = 5000000;
    cmd.quantity = 10;

    auto fix = adapter.build_new_ack(cmd, 123456);
    EXPECT_NE(fix.find("37=123456"), std::string::npos);
    EXPECT_NE(fix.find("11=7777"), std::string::npos);
    EXPECT_NE(fix.find("35=8"), std::string::npos);
}

TEST(RealFixAdapterTest, BuildsFillMessage) {
    RealFixAdapter adapter(test_symbol_resolver, test_price_converter, test_qty_converter);

    auto fix = adapter.build_fill(100, 5000000, 10, 10, 0, engine::Side::Buy);
    EXPECT_NE(fix.find("31=5000000"), std::string::npos);  // LastPx
    EXPECT_NE(fix.find("39=2"), std::string::npos);         // OrdStatus=Filled
}

TEST(RealFixAdapterTest, ParsesCustomSTPTag) {
    RealFixAdapter adapter(test_symbol_resolver, test_price_converter, test_qty_converter);

    std::string fix = "8=FIX.4.4|35=D|11=8001|55=BTC-USD|54=1|40=2|44=50000|38=10|20001=42|";
    auto cmd = adapter.parse_new_order(fix);

    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->stp_group_id, 42u);
}

}  // namespace
}  // namespace gateway
