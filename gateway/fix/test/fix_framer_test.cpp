#include "gateway/fix_framer.h"
#include "gateway/fix_parser.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace gateway {
namespace {

constexpr char SOH = '\x01';

/// Build a well-formed FIX message with correct BodyLength and CheckSum.
std::string make_msg(const std::string& body) {
    std::string pre = std::string("8=FIX.4.4") + SOH + "9=" + std::to_string(body.size()) + SOH + body;
    return pre + "10=" + compute_fix_checksum(pre) + SOH;
}

std::string heartbeat(int seq) {
    return make_msg(std::string("35=0") + SOH + "49=A" + SOH + "56=B" + SOH +
                    "34=" + std::to_string(seq) + SOH);
}

TEST(FixFramer, SingleCompleteMessage) {
    const std::string m = heartbeat(1);
    FixStreamFramer f;
    f.append(m.data(), m.size());

    std::string out;
    ASSERT_EQ(f.next(out), FrameStatus::Complete);
    EXPECT_EQ(out, m);
    EXPECT_EQ(f.buffered(), 0u);
    EXPECT_EQ(f.next(out), FrameStatus::NeedMore);
}

TEST(FixFramer, TwoConcatenatedMessages) {
    const std::string a = heartbeat(1);
    const std::string b = heartbeat(2);
    const std::string both = a + b;
    FixStreamFramer f;
    f.append(both.data(), both.size());

    std::string out;
    ASSERT_EQ(f.next(out), FrameStatus::Complete);
    EXPECT_EQ(out, a);
    ASSERT_EQ(f.next(out), FrameStatus::Complete);
    EXPECT_EQ(out, b);
    EXPECT_EQ(f.next(out), FrameStatus::NeedMore);
}

TEST(FixFramer, MessageSplitAcrossTwoReads) {
    const std::string m = heartbeat(7);
    // Split at every possible offset, including inside tag 9 and the trailer.
    for (size_t cut = 1; cut < m.size(); ++cut) {
        FixStreamFramer f;
        std::string out;
        f.append(m.data(), cut);
        ASSERT_EQ(f.next(out), FrameStatus::NeedMore) << "cut=" << cut;
        f.append(m.data() + cut, m.size() - cut);
        ASSERT_EQ(f.next(out), FrameStatus::Complete) << "cut=" << cut;
        EXPECT_EQ(out, m) << "cut=" << cut;
    }
}

TEST(FixFramer, ByteAtATime) {
    const std::string a = heartbeat(1);
    const std::string b = heartbeat(2);
    const std::string both = a + b;
    FixStreamFramer f;
    std::string out;
    std::vector<std::string> got;
    for (char c : both) {
        f.append(&c, 1);
        while (f.next(out) == FrameStatus::Complete) got.push_back(out);
    }
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0], a);
    EXPECT_EQ(got[1], b);
}

TEST(FixFramer, TrailingJunkIsErrorAfterFirstMessageDelivered) {
    const std::string m = heartbeat(1);
    const std::string data = m + "GARBAGE";
    FixStreamFramer f;
    f.append(data.data(), data.size());

    std::string out;
    ASSERT_EQ(f.next(out), FrameStatus::Complete);
    EXPECT_EQ(out, m);
    EXPECT_EQ(f.next(out), FrameStatus::Error);
}

TEST(FixFramer, TrailingPartialBeginStringWaits) {
    const std::string m = heartbeat(1);
    const std::string data = m + "8=F";
    FixStreamFramer f;
    f.append(data.data(), data.size());

    std::string out;
    ASSERT_EQ(f.next(out), FrameStatus::Complete);
    EXPECT_EQ(f.next(out), FrameStatus::NeedMore);
}

TEST(FixFramer, NonNumericBodyLengthIsError) {
    const std::string bad = std::string("8=FIX.4.4") + SOH + "9=abc" + SOH + "35=0" + SOH +
                            "10=000" + SOH;
    FixStreamFramer f;
    f.append(bad.data(), bad.size());
    std::string out;
    EXPECT_EQ(f.next(out), FrameStatus::Error);
}

TEST(FixFramer, EmptyBodyLengthIsError) {
    const std::string bad = std::string("8=FIX.4.4") + SOH + "9=" + SOH + "35=0" + SOH;
    EXPECT_EQ(frame_next_message(bad).status, FrameStatus::Error);
}

TEST(FixFramer, OversizedBodyLengthIsError) {
    const std::string bad = std::string("8=FIX.4.4") + SOH + "9=99999999" + SOH;
    EXPECT_EQ(frame_next_message(bad).status, FrameStatus::Error);

    const std::string just_over =
        std::string("8=FIX.4.4") + SOH + "9=" + std::to_string(kFixMaxBodyLength + 1) + SOH;
    EXPECT_EQ(frame_next_message(just_over).status, FrameStatus::Error);
}

TEST(FixFramer, BodyLengthNotLandingOnTrailerIsError) {
    // Claimed length is shorter than actual body, so trailer check fails.
    std::string body = std::string("35=0") + SOH + "49=A" + SOH + "56=B" + SOH + "34=1" + SOH;
    std::string pre = std::string("8=FIX.4.4") + SOH + "9=" + std::to_string(body.size() - 2) +
                      SOH + body;
    std::string bad = pre + "10=" + compute_fix_checksum(pre) + SOH;
    EXPECT_EQ(frame_next_message(bad).status, FrameStatus::Error);
}

TEST(FixFramer, MissingBodyLengthFallsBackToChecksumTrailer) {
    std::string pre = std::string("8=FIX.4.4") + SOH + "35=0" + SOH + "34=1" + SOH;
    std::string m = pre + "10=" + compute_fix_checksum(pre) + SOH;
    std::string data = m + m;
    auto r = frame_next_message(data);
    ASSERT_EQ(r.status, FrameStatus::Complete);
    EXPECT_EQ(r.length, m.size());
    EXPECT_EQ(frame_next_message(pre).status, FrameStatus::NeedMore);
}

TEST(FixFramer, NotFixIsError) {
    EXPECT_EQ(frame_next_message("GET / HTTP/1.1\r\n").status, FrameStatus::Error);
    EXPECT_EQ(frame_next_message("8=FIX").status, FrameStatus::NeedMore);
    EXPECT_EQ(frame_next_message("8=FIy").status, FrameStatus::Error);
}

}  // namespace
}  // namespace gateway
