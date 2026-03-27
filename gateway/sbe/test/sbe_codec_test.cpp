#include "gateway/sbe_codec.h"
#include <gtest/gtest.h>
#include <vector>

namespace gateway {
namespace {

TEST(SbeCodecTest, DecodeHeader) {
    uint8_t buf[SbeHeader::SIZE] = {};
    // Write a header manually
    *reinterpret_cast<uint16_t*>(buf + 0) = 57;   // block_length
    *reinterpret_cast<uint16_t*>(buf + 2) = 1;    // template_id = NewOrder
    *reinterpret_cast<uint16_t*>(buf + 4) = 1;    // schema_id
    *reinterpret_cast<uint16_t*>(buf + 6) = 1;    // version
    *reinterpret_cast<uint64_t*>(buf + 8) = 42;   // sequence
    *reinterpret_cast<uint64_t*>(buf + 16) = 9999; // sending_time

    auto hdr = SbeCodec::decode_header(buf, sizeof(buf));
    ASSERT_TRUE(hdr.has_value());
    EXPECT_EQ(hdr->block_length, 57);
    EXPECT_EQ(hdr->template_id, 1);
    EXPECT_EQ(hdr->sequence_number, 42u);
    EXPECT_EQ(hdr->sending_time, 9999u);
}

TEST(SbeCodecTest, DecodeHeaderTooShort) {
    uint8_t buf[10] = {};
    EXPECT_FALSE(SbeCodec::decode_header(buf, sizeof(buf)).has_value());
}

TEST(SbeCodecTest, DecodeNewOrder) {
    // Build a 57-byte NewOrder body
    std::vector<uint8_t> body(57, 0);
    size_t off = 0;

    *reinterpret_cast<uint64_t*>(body.data() + off) = 5001;  off += 8;  // ClientOrderId
    *reinterpret_cast<uint32_t*>(body.data() + off) = 100;   off += 4;  // InstrumentId
    body[off++] = 1;  // Side::Buy
    body[off++] = 2;  // OrderType::Limit
    body[off++] = 3;  // TimeInForce::IOC
    *reinterpret_cast<int64_t*>(body.data() + off) = 50000;  off += 8;  // Price
    *reinterpret_cast<uint64_t*>(body.data() + off) = 200;   off += 8;  // Quantity
    off += 8;  // display_qty (skip)
    off += 8;  // stop_price (skip)
    *reinterpret_cast<uint32_t*>(body.data() + off) = 42;    off += 4;  // STPGroupId
    *reinterpret_cast<uint32_t*>(body.data() + off) = 7;     off += 4;  // AccountId
    *reinterpret_cast<uint16_t*>(body.data() + off) = 0x01;  off += 2;  // Flags: PostOnly

    auto cmd = SbeCodec::decode_new_order(body.data(), body.size());
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->client_order_id, 5001u);
    EXPECT_EQ(cmd->instrument_id, 100u);
    EXPECT_EQ(cmd->side, engine::Side::Buy);
    EXPECT_EQ(cmd->order_type, engine::OrderType::Limit);
    EXPECT_EQ(cmd->time_in_force, engine::TimeInForce::IOC);
    EXPECT_EQ(cmd->price, 50000);
    EXPECT_EQ(cmd->quantity, 200u);
    EXPECT_EQ(cmd->stp_group_id, 42u);
    EXPECT_EQ(cmd->account_id, 7u);
    EXPECT_EQ(cmd->flags, engine::FLAG_POST_ONLY);
}

TEST(SbeCodecTest, DecodeCancelOrder) {
    std::vector<uint8_t> body(20, 0);
    *reinterpret_cast<uint64_t*>(body.data() + 0) = 99999;   // OrigOrderId
    *reinterpret_cast<uint64_t*>(body.data() + 8) = 6001;    // ClientOrderId
    *reinterpret_cast<uint32_t*>(body.data() + 16) = 100;    // InstrumentId

    auto cmd = SbeCodec::decode_cancel(body.data(), body.size());
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->orig_order_id, 99999u);
    EXPECT_EQ(cmd->client_order_id, 6001u);
    EXPECT_EQ(cmd->instrument_id, 100u);
}

TEST(SbeCodecTest, DecodeModifyOrder) {
    std::vector<uint8_t> body(36, 0);
    *reinterpret_cast<uint64_t*>(body.data() + 0) = 88888;
    *reinterpret_cast<uint64_t*>(body.data() + 8) = 7001;
    *reinterpret_cast<uint32_t*>(body.data() + 16) = 100;
    *reinterpret_cast<int64_t*>(body.data() + 20) = 51000;
    *reinterpret_cast<uint64_t*>(body.data() + 28) = 50;

    auto cmd = SbeCodec::decode_modify(body.data(), body.size());
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->orig_order_id, 88888u);
    EXPECT_EQ(cmd->new_price, 51000);
    EXPECT_EQ(cmd->new_quantity, 50u);
}

TEST(SbeCodecTest, EncodeExecReport) {
    uint8_t buf[256] = {};
    size_t written = SbeCodec::encode_exec_report(
        buf, sizeof(buf),
        12345,   // order_id
        67890,   // client_order_id
        100,     // instrument_id
        engine::Side::Buy,
        2,       // ord_status = Filled
        1,       // exec_type = Trade
        50000,   // last_px
        100,     // last_qty
        100,     // cum_qty
        0,       // leaves_qty
        42,      // sequence
        9999     // timestamp
    );

    EXPECT_EQ(written, SbeHeader::SIZE + 82);

    // Verify header
    auto hdr = SbeCodec::decode_header(buf, written);
    ASSERT_TRUE(hdr.has_value());
    EXPECT_EQ(hdr->template_id, sbe_template::ExecReport);
    EXPECT_EQ(hdr->sequence_number, 42u);
}

TEST(SbeCodecTest, EncodeHeartbeat) {
    uint8_t buf[64] = {};
    size_t written = SbeCodec::encode_heartbeat(buf, sizeof(buf), 100, 5000);
    EXPECT_EQ(written, SbeHeader::SIZE);

    auto hdr = SbeCodec::decode_header(buf, written);
    ASSERT_TRUE(hdr.has_value());
    EXPECT_EQ(hdr->template_id, sbe_template::Heartbeat);
    EXPECT_EQ(hdr->sequence_number, 100u);
}

TEST(SbeCodecTest, EncodeTooSmallBufferReturnsZero) {
    uint8_t buf[10] = {};
    size_t written = SbeCodec::encode_heartbeat(buf, 10, 1, 1);
    EXPECT_EQ(written, 0u);
}

}  // namespace
}  // namespace gateway
