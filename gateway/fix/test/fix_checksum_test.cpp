#include "gateway/fix_parser.h"

#include <gtest/gtest.h>

#include <string>

using namespace gateway;

namespace {
RealFixAdapter make_adapter() {
    return RealFixAdapter(
        [](const std::string&) { return engine::InstrumentId{1}; },
        [](const std::string&, engine::InstrumentId) { return engine::Price{0}; },
        [](const std::string&, engine::InstrumentId) { return engine::Quantity{0}; });
}
}  // namespace

TEST(FixChecksum, ComputesKnownValue) {
    // Bytes of "8=FIX.4.4|35=0|" with SOH delimiters sum to 247 (mod 256).
    EXPECT_EQ(compute_fix_checksum("8=FIX.4.4\x01" "35=0\x01"), "247");
}

TEST(FixChecksum, ZeroPadsToThreeDigits) {
    EXPECT_EQ(compute_fix_checksum(""), "000");
    EXPECT_EQ(compute_fix_checksum("\x05"), "005");
    EXPECT_EQ(compute_fix_checksum("\x2a"), "042");
}

TEST(FixChecksum, WrapsModulo256) {
    // 'A' (65) * 4 = 260 -> 4
    EXPECT_EQ(compute_fix_checksum("AAAA"), "004");
}

TEST(FixChecksum, BuildNewAckHasValidChecksum) {
    auto adapter = make_adapter();
    NewOrderCommand cmd{};
    cmd.client_order_id = 7;
    cmd.instrument_id = 1;
    cmd.side = engine::Side::Buy;
    cmd.price = 1000;
    cmd.quantity = 5;

    const std::string ack = adapter.build_new_ack(cmd, 42);

    const auto pos = ack.rfind("\x01" "10=");
    ASSERT_NE(pos, std::string::npos);
    const std::string body = ack.substr(0, pos + 1);
    EXPECT_EQ(ack.substr(pos + 1), "10=" + compute_fix_checksum(body) + "\x01");
    EXPECT_NE(ack.substr(pos + 1), "10=000\x01");  // no longer the placeholder
    EXPECT_TRUE(verify_checksum(ack));
}

TEST(FixChecksum, AllBuildersProduceValidChecksums) {
    auto adapter = make_adapter();
    EXPECT_TRUE(verify_checksum(adapter.build_cancel_ack(9)));
    EXPECT_TRUE(verify_checksum(adapter.build_reject(3, engine::RejectCode{})));
    EXPECT_TRUE(verify_checksum(adapter.build_fill(1, 100, 2, 2, 0, engine::Side::Sell)));
    EXPECT_TRUE(verify_checksum(adapter.build_order_status_response(
        1, engine::Side::Buy, 100, 3, 2, "1")));
}

TEST(FixChecksum, VerifyAcceptsValidMessage) {
    const std::string body = "8=FIX.4.4\x01" "35=0\x01";
    EXPECT_TRUE(verify_checksum(body + "10=247\x01"));
}

TEST(FixChecksum, VerifyRejectsGarbledMessage) {
    // Wrong checksum value
    EXPECT_FALSE(verify_checksum("8=FIX.4.4\x01" "35=0\x01" "10=248\x01"));
    // Body altered after checksum was computed
    EXPECT_FALSE(verify_checksum("8=FIX.4.4\x01" "35=1\x01" "10=247\x01"));
    // Malformed checksum field
    EXPECT_FALSE(verify_checksum("8=FIX.4.4\x01" "35=0\x01" "10=24\x01"));
    EXPECT_FALSE(verify_checksum("8=FIX.4.4\x01" "35=0\x01" "10=abc\x01"));
}

TEST(FixChecksum, VerifyPassesWhenNoChecksumField) {
    EXPECT_TRUE(verify_checksum("8=FIX.4.4\x01" "35=0\x01"));
}
