#include "gateway/fix_gap_handler.h"

#include <gtest/gtest.h>
#include <string>

using gateway::FixGapHandler;

TEST(FixGapHandlerTest, DetectsGap) {
    // received=5, expected=3 -> gap detected
    EXPECT_TRUE(FixGapHandler::is_gap(5, 3));

    // received=3, expected=3 -> no gap
    EXPECT_FALSE(FixGapHandler::is_gap(3, 3));
}

TEST(FixGapHandlerTest, NoGapWhenSequenceMatches) {
    EXPECT_FALSE(FixGapHandler::is_gap(1, 1));
    EXPECT_FALSE(FixGapHandler::is_gap(100, 100));
}

TEST(FixGapHandlerTest, HandleSequenceTooLow) {
    // received < expected is a possible duplicate, not a gap
    EXPECT_FALSE(FixGapHandler::is_gap(2, 5));
    EXPECT_FALSE(FixGapHandler::is_gap(1, 3));
}

TEST(FixGapHandlerTest, BuildResendRequest) {
    auto msg = FixGapHandler::build_resend_request(3, 5, "SENDER", "TARGET", 10);

    // Verify it contains the key tags
    EXPECT_NE(msg.find("35=2"), std::string::npos) << "Missing MsgType=2 (ResendRequest)";
    EXPECT_NE(msg.find("7=3"), std::string::npos) << "Missing BeginSeqNo=3";
    EXPECT_NE(msg.find("16=5"), std::string::npos) << "Missing EndSeqNo=5";
    EXPECT_NE(msg.find("49=SENDER"), std::string::npos) << "Missing SenderCompID";
    EXPECT_NE(msg.find("56=TARGET"), std::string::npos) << "Missing TargetCompID";
    EXPECT_NE(msg.find("34=10"), std::string::npos) << "Missing MsgSeqNum";
    EXPECT_NE(msg.find("8=FIX.4.4"), std::string::npos) << "Missing BeginString";
}

TEST(FixGapHandlerTest, BuildResendRequestInfinity) {
    // end_seq=0 means "all messages from begin_seq"
    auto msg = FixGapHandler::build_resend_request(3, 0, "SENDER", "TARGET", 10);
    EXPECT_NE(msg.find("7=3"), std::string::npos);
    EXPECT_NE(msg.find("16=0"), std::string::npos);
}

TEST(FixGapHandlerTest, BuildGapFill) {
    auto msg = FixGapHandler::build_gap_fill(10, 5, "SENDER", "TARGET");

    EXPECT_NE(msg.find("35=4"), std::string::npos) << "Missing MsgType=4 (SequenceReset)";
    EXPECT_NE(msg.find("123=Y"), std::string::npos) << "Missing GapFillFlag=Y";
    EXPECT_NE(msg.find("36=10"), std::string::npos) << "Missing NewSeqNo=10";
    EXPECT_NE(msg.find("34=5"), std::string::npos) << "Missing MsgSeqNum=5";
    EXPECT_NE(msg.find("49=SENDER"), std::string::npos);
    EXPECT_NE(msg.find("56=TARGET"), std::string::npos);
}

TEST(FixGapHandlerTest, BuildSequenceReset) {
    auto msg = FixGapHandler::build_sequence_reset(20, "SENDER", "TARGET", 1);

    EXPECT_NE(msg.find("35=4"), std::string::npos) << "Missing MsgType=4";
    EXPECT_NE(msg.find("123=N"), std::string::npos) << "Missing GapFillFlag=N (hard reset)";
    EXPECT_NE(msg.find("36=20"), std::string::npos) << "Missing NewSeqNo=20";
    EXPECT_NE(msg.find("34=1"), std::string::npos) << "Missing MsgSeqNum";
}

TEST(FixGapHandlerTest, ParseResendRequest) {
    // Build a ResendRequest and then parse it
    auto msg = FixGapHandler::build_resend_request(3, 7, "SENDER", "TARGET", 10);
    auto parsed = FixGapHandler::parse_resend_request(msg);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->begin_seq, 3u);
    EXPECT_EQ(parsed->end_seq, 7u);
}

TEST(FixGapHandlerTest, ParseResendRequestInfinity) {
    auto msg = FixGapHandler::build_resend_request(5, 0, "S", "T", 1);
    auto parsed = FixGapHandler::parse_resend_request(msg);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->begin_seq, 5u);
    EXPECT_EQ(parsed->end_seq, 0u);
}

TEST(FixGapHandlerTest, ParseSequenceResetGapFill) {
    auto msg = FixGapHandler::build_gap_fill(15, 5, "SENDER", "TARGET");
    auto parsed = FixGapHandler::parse_sequence_reset(msg);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->new_seq_no, 15u);
    EXPECT_TRUE(parsed->is_gap_fill);
}

TEST(FixGapHandlerTest, ParseSequenceResetHardReset) {
    auto msg = FixGapHandler::build_sequence_reset(50, "SENDER", "TARGET", 1);
    auto parsed = FixGapHandler::parse_sequence_reset(msg);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->new_seq_no, 50u);
    EXPECT_FALSE(parsed->is_gap_fill);
}

TEST(FixGapHandlerTest, ParseResendRequestInvalidMsgType) {
    // A non-ResendRequest message should return nullopt
    auto msg = FixGapHandler::build_gap_fill(10, 5, "S", "T");
    auto parsed = FixGapHandler::parse_resend_request(msg);
    EXPECT_FALSE(parsed.has_value());
}

TEST(FixGapHandlerTest, ParseSequenceResetInvalidMsgType) {
    // A non-SequenceReset message should return nullopt
    auto msg = FixGapHandler::build_resend_request(1, 5, "S", "T", 1);
    auto parsed = FixGapHandler::parse_sequence_reset(msg);
    EXPECT_FALSE(parsed.has_value());
}
