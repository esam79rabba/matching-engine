#include "gateway/fix_gateway_adapter.h"
#include <gtest/gtest.h>

namespace gateway {
namespace {

TEST(FixAdapterStubTest, ParseNewOrderFromNonEmptyMessage) {
    FixGatewayAdapterStub adapter;
    auto cmd = adapter.parse_new_order("8=FIX.4.4|35=D|...");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->instrument_id, 100u);
    EXPECT_EQ(cmd->side, engine::Side::Buy);
    EXPECT_EQ(cmd->price, 10000);
    EXPECT_EQ(cmd->quantity, 100u);
}

TEST(FixAdapterStubTest, ParseNewOrderFromEmptyReturnsNullopt) {
    FixGatewayAdapterStub adapter;
    auto cmd = adapter.parse_new_order("");
    EXPECT_FALSE(cmd.has_value());
}

TEST(FixAdapterStubTest, ParseCancelFromNonEmptyMessage) {
    FixGatewayAdapterStub adapter;
    auto cmd = adapter.parse_cancel("8=FIX.4.4|35=F|...");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->orig_order_id, 1u);
}

TEST(FixAdapterStubTest, BuildNewAckContainsOrderId) {
    FixGatewayAdapterStub adapter;
    NewOrderCommand cmd{};
    cmd.client_order_id = 5555;
    cmd.instrument_id = 100;
    cmd.side = engine::Side::Buy;
    cmd.price = 10000;
    cmd.quantity = 50;

    auto fix_msg = adapter.build_new_ack(cmd, 99999);
    EXPECT_NE(fix_msg.find("37=99999"), std::string::npos);   // OrderID present
    EXPECT_NE(fix_msg.find("11=5555"), std::string::npos);    // ClOrdID present
}

TEST(FixAdapterStubTest, BuildFillContainsPrice) {
    FixGatewayAdapterStub adapter;
    auto fix_msg = adapter.build_fill(1, 10050, 100, 100, 0, engine::Side::Buy);
    EXPECT_NE(fix_msg.find("31=10050"), std::string::npos);   // LastPx
    EXPECT_NE(fix_msg.find("32=100"), std::string::npos);     // LastQty
    EXPECT_NE(fix_msg.find("39=2"), std::string::npos);       // OrdStatus=Filled
}

TEST(FixAdapterStubTest, BuildRejectContainsReasonCode) {
    FixGatewayAdapterStub adapter;
    auto fix_msg = adapter.build_reject(1234, engine::RejectCode::PriceOutsideBands);
    EXPECT_NE(fix_msg.find("103=3"), std::string::npos);      // CxlRejReason=3
}

TEST(FixAdapterStubTest, SessionHooksTrackCounts) {
    FixGatewayAdapterStub adapter;
    EXPECT_EQ(adapter.logon_count, 0u);

    adapter.on_session_logon(1, 100);
    adapter.on_session_logon(2, 101);
    EXPECT_EQ(adapter.logon_count, 2u);

    adapter.on_session_disconnect(1);
    EXPECT_EQ(adapter.disconnect_count, 1u);

    adapter.on_session_logout(2);
    EXPECT_EQ(adapter.logout_count, 1u);
}

}  // namespace
}  // namespace gateway
