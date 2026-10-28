/// Integration tests for FIX gap handling wired into FixTcpAcceptor.
///
/// Tests use a real FixTcpAcceptor with TcpServerSocket, connecting via raw TCP
/// to verify gap detection, ResendRequest, SequenceReset, and GapFill behavior.

#include "gateway/tcp/fix_tcp_acceptor.h"
#include "gateway/fix_gap_handler.h"
#include "gateway/fix_parser.h"
#include "transport/tcp_socket.h"

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>

namespace gateway {
namespace {

static constexpr char SOH = '\x01';

/// Build a minimal FIX Logon message.
static std::string make_logon(const std::string& sender, const std::string& target,
                               uint64_t seq = 1, int hb = 30) {
    std::string msg;
    msg += "8=FIX.4.4"; msg += SOH;
    msg += "35=A"; msg += SOH;
    msg += "49=" + sender; msg += SOH;
    msg += "56=" + target; msg += SOH;
    msg += "34=" + std::to_string(seq); msg += SOH;
    msg += "108=" + std::to_string(hb); msg += SOH;
    msg += "10=" + gateway::compute_fix_checksum(msg); msg += SOH;
    return msg;
}

/// Build a minimal FIX NewOrderSingle (35=D) with a given sequence number.
static std::string make_new_order(const std::string& sender, const std::string& target,
                                   uint64_t seq) {
    std::string msg;
    msg += "8=FIX.4.4"; msg += SOH;
    msg += "35=D"; msg += SOH;
    msg += "49=" + sender; msg += SOH;
    msg += "56=" + target; msg += SOH;
    msg += "34=" + std::to_string(seq); msg += SOH;
    msg += "11=ORD001"; msg += SOH;
    msg += "55=BTC-USD"; msg += SOH;
    msg += "54=1"; msg += SOH;
    msg += "38=100"; msg += SOH;
    msg += "44=50000"; msg += SOH;
    msg += "40=2"; msg += SOH;
    msg += "10=" + gateway::compute_fix_checksum(msg); msg += SOH;
    return msg;
}

/// Build a SequenceReset-GapFill (35=4, 123=Y).
static std::string make_seq_reset_gap_fill(const std::string& sender, const std::string& target,
                                            uint64_t seq, uint64_t new_seq) {
    std::string msg;
    msg += "8=FIX.4.4"; msg += SOH;
    msg += "35=4"; msg += SOH;
    msg += "49=" + sender; msg += SOH;
    msg += "56=" + target; msg += SOH;
    msg += "34=" + std::to_string(seq); msg += SOH;
    msg += "123=Y"; msg += SOH;
    msg += "36=" + std::to_string(new_seq); msg += SOH;
    msg += "10=" + gateway::compute_fix_checksum(msg); msg += SOH;
    return msg;
}

/// Build a ResendRequest (35=2).
static std::string make_resend_request(const std::string& sender, const std::string& target,
                                        uint64_t seq, uint64_t begin_seq, uint64_t end_seq) {
    std::string msg;
    msg += "8=FIX.4.4"; msg += SOH;
    msg += "35=2"; msg += SOH;
    msg += "49=" + sender; msg += SOH;
    msg += "56=" + target; msg += SOH;
    msg += "34=" + std::to_string(seq); msg += SOH;
    msg += "7=" + std::to_string(begin_seq); msg += SOH;
    msg += "16=" + std::to_string(end_seq); msg += SOH;
    msg += "10=" + gateway::compute_fix_checksum(msg); msg += SOH;
    return msg;
}

/// Helper: recv with timeout (returns empty string on timeout/error).
static std::string recv_with_timeout(int fd, int timeout_ms = 500) {
    struct timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char buf[4096]{};
    ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return "";
    return std::string(buf, static_cast<size_t>(n));
}

// ─── Test Fixture ────────────────────────────────────────────

class FixGapIntegrationTest : public ::testing::Test {
protected:
    transport::TcpServerSocket* tcp_server_ = nullptr;
    FixTcpAcceptor* acceptor_ = nullptr;
    std::thread server_thread_;
    uint16_t port_ = 0;

    void SetUp() override {
        tcp_server_ = new transport::TcpServerSocket(0);
        ASSERT_TRUE(tcp_server_->listen());
        port_ = tcp_server_->bound_port();
        ASSERT_GT(port_, 0u);

        acceptor_ = new FixTcpAcceptor(port_);
        acceptor_->set_server_socket(tcp_server_);
        acceptor_->set_comp_id_resolver([](const std::string&) -> std::optional<engine::ParticipantId> { return engine::ParticipantId{1}; });
    }

    void start_acceptor() {
        server_thread_ = std::thread([this]() {
            acceptor_->start();
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void TearDown() override {
        if (acceptor_) {
            acceptor_->stop();
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        delete acceptor_;
        delete tcp_server_;
    }
};

// ─── Tests ───────────────────────────────────────────────────

// Each test uses ONE connection: Logon, read the ack, then application
// messages on the same stream. Application messages before Logon are
// rejected fail-closed (app_msg_before_logon), so they are not used here.

static int open_conn(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

TEST_F(FixGapIntegrationTest, GapDetectedSendsResendRequest) {
    std::atomic<bool> message_received{false};

    FixTcpCallbacks cbs;
    cbs.on_logon = [](engine::SessionId, engine::ParticipantId) {};
    cbs.on_message = [&](engine::SessionId, const std::string&) {
        message_received = true;
    };
    acceptor_->set_callbacks(cbs);
    start_acceptor();

    int fd = open_conn(port_);
    ASSERT_GE(fd, 0);

    std::string logon = make_logon("CLIENT", "SERVER", 1);
    ::send(fd, logon.c_str(), logon.size(), 0);
    std::string ack = recv_with_timeout(fd);
    ASSERT_FALSE(ack.empty()) << "Expected logon ack";

    // Expected inbound seq is 2; send seq=5 -> gap of 2..4.
    std::string order = make_new_order("CLIENT", "SERVER", 5);
    ::send(fd, order.c_str(), order.size(), 0);

    std::string resp = recv_with_timeout(fd);
    ASSERT_FALSE(resp.empty()) << "Expected ResendRequest";
    auto fields = FixParser::parse_fields(resp);
    auto mt = FixParser::get_msg_type(fields);
    ASSERT_TRUE(mt.has_value());
    EXPECT_EQ(*mt, "2");
    EXPECT_FALSE(message_received) << "Out-of-sequence message must not be delivered";

    ::close(fd);
}

TEST_F(FixGapIntegrationTest, SequenceResetAdvancesExpected) {
    std::atomic<bool> message_received{false};

    FixTcpCallbacks cbs;
    cbs.on_logon = [](engine::SessionId, engine::ParticipantId) {};
    cbs.on_message = [&](engine::SessionId, const std::string&) {
        message_received = true;
    };
    acceptor_->set_callbacks(cbs);
    start_acceptor();

    int fd = open_conn(port_);
    ASSERT_GE(fd, 0);

    std::string logon = make_logon("CLIENT", "SERVER", 1);
    ::send(fd, logon.c_str(), logon.size(), 0);
    ASSERT_FALSE(recv_with_timeout(fd).empty()) << "Expected logon ack";

    // GapFill advances expected inbound seq to 10; the order at seq=10 then
    // arrives in order. Sent back-to-back to exercise multi-message reads.
    std::string both = make_seq_reset_gap_fill("CLIENT", "SERVER", 2, 10) +
                       make_new_order("CLIENT", "SERVER", 10);
    ::send(fd, both.c_str(), both.size(), 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_TRUE(message_received);

    ::close(fd);
}

TEST_F(FixGapIntegrationTest, NoGapWhenSequenceCorrect) {
    std::atomic<int> message_count{0};

    FixTcpCallbacks cbs;
    cbs.on_logon = [](engine::SessionId, engine::ParticipantId) {};
    cbs.on_message = [&](engine::SessionId, const std::string&) {
        message_count++;
    };
    acceptor_->set_callbacks(cbs);
    start_acceptor();

    int fd = open_conn(port_);
    ASSERT_GE(fd, 0);

    std::string logon = make_logon("CLIENT", "SERVER", 1);
    ::send(fd, logon.c_str(), logon.size(), 0);
    ASSERT_FALSE(recv_with_timeout(fd).empty()) << "Expected logon ack";

    // Logon consumed seq 1; seq 2 is the next expected -> no gap.
    std::string order = make_new_order("CLIENT", "SERVER", 2);
    ::send(fd, order.c_str(), order.size(), 0);

    std::string response = recv_with_timeout(fd, 200);
    if (!response.empty()) {
        auto fields = FixParser::parse_fields(response);
        auto mt = FixParser::get_msg_type(fields);
        if (mt.has_value()) {
            EXPECT_NE(*mt, "2") << "Should not receive ResendRequest for correct sequence";
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(message_count, 1) << "Message should have been processed normally";

    ::close(fd);
}

TEST_F(FixGapIntegrationTest, ResendRequestRespondsWithGapFill) {
    FixTcpCallbacks cbs;
    cbs.on_logon = [](engine::SessionId, engine::ParticipantId) {};
    acceptor_->set_callbacks(cbs);
    start_acceptor();

    // Send a ResendRequest (35=2) with BeginSeqNo=1, EndSeqNo=5
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(fd, 0);
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port_);
    ASSERT_EQ(::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)), 0);

    std::string resend_req = make_resend_request("CLIENT", "SERVER", 1, 1, 5);
    ::send(fd, resend_req.c_str(), resend_req.size(), 0);

    // Should receive a GapFill (35=4, 123=Y) back
    std::string response = recv_with_timeout(fd);
    EXPECT_FALSE(response.empty()) << "Expected GapFill response";

    if (!response.empty()) {
        auto fields = FixParser::parse_fields(response);
        auto mt = FixParser::get_msg_type(fields);
        ASSERT_TRUE(mt.has_value());
        EXPECT_EQ(*mt, "4") << "Expected MsgType=4 (SequenceReset)";

        // Verify it's a GapFill
        EXPECT_NE(response.find("123=Y"), std::string::npos) << "Expected GapFillFlag=Y";

        // NewSeqNo should be end_seq+1 = 6
        EXPECT_NE(response.find("36=6"), std::string::npos) << "Expected NewSeqNo=6";
    }

    ::close(fd);
}

}  // namespace
}  // namespace gateway
