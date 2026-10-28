/// Tests for PossDupFlag (tag 43=Y) injection on FIX message replay.
///
/// B2: Full FIX Replay from Persistent Store — PossDupFlag handling.
///
/// Tests:
///   - add_poss_dup_flag() helper inserts tag correctly
///   - add_poss_dup_flag() does not double-insert
///   - add_poss_dup_flag() handles edge cases
///   - handle_resend_request sends 43=Y in replayed messages (integration)

#include "gateway/tcp/fix_tcp_acceptor.h"
#include "gateway/fix_gap_handler.h"
#include "gateway/fix_message_store.h"
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

namespace gateway {
namespace {

static constexpr char SOH = '\x01';

// ─── Unit tests for add_poss_dup_flag() ──────────────────────

TEST(AddPossDupFlagTest, InsertsAfterBeginString) {
    std::string msg;
    msg += "8=FIX.4.4"; msg += SOH;
    msg += "35=D"; msg += SOH;
    msg += "34=5"; msg += SOH;
    msg += "10=000"; msg += SOH;

    auto result = FixTcpAcceptor::add_poss_dup_flag(msg);

    // Should have 43=Y right after 8=FIX.4.4\x01
    std::string expected_prefix;
    expected_prefix += "8=FIX.4.4"; expected_prefix += SOH;
    expected_prefix += "43=Y"; expected_prefix += SOH;
    EXPECT_EQ(result.substr(0, expected_prefix.size()), expected_prefix)
        << "43=Y should be inserted immediately after BeginString";

    // Should still contain the rest of the message
    EXPECT_NE(result.find("35=D"), std::string::npos);
    EXPECT_NE(result.find("34=5"), std::string::npos);
    EXPECT_NE(result.find("10=000"), std::string::npos);
}

TEST(AddPossDupFlagTest, DoesNotDoubleInsert) {
    std::string msg;
    msg += "8=FIX.4.4"; msg += SOH;
    msg += "43=Y"; msg += SOH;
    msg += "35=D"; msg += SOH;
    msg += "10=000"; msg += SOH;

    auto result = FixTcpAcceptor::add_poss_dup_flag(msg);

    // Should be unchanged — already has 43=
    EXPECT_EQ(result, msg) << "Should not modify a message that already has tag 43";
}

TEST(AddPossDupFlagTest, HandlesEmptyMessage) {
    auto result = FixTcpAcceptor::add_poss_dup_flag("");
    EXPECT_EQ(result, "");
}

TEST(AddPossDupFlagTest, HandlesMessageWithoutSOH) {
    std::string msg = "8=FIX.4.4";
    auto result = FixTcpAcceptor::add_poss_dup_flag(msg);
    EXPECT_EQ(result, msg) << "No SOH means no insertion point; return unchanged";
}

TEST(AddPossDupFlagTest, PreservesOriginalContent) {
    std::string msg;
    msg += "8=FIX.4.4"; msg += SOH;
    msg += "35=8"; msg += SOH;
    msg += "49=SERVER"; msg += SOH;
    msg += "56=CLIENT"; msg += SOH;
    msg += "34=3"; msg += SOH;
    msg += "150=0"; msg += SOH;
    msg += "10=123"; msg += SOH;

    auto result = FixTcpAcceptor::add_poss_dup_flag(msg);

    // All original tags should be present
    EXPECT_NE(result.find("35=8"), std::string::npos);
    EXPECT_NE(result.find("49=SERVER"), std::string::npos);
    EXPECT_NE(result.find("56=CLIENT"), std::string::npos);
    EXPECT_NE(result.find("34=3"), std::string::npos);
    EXPECT_NE(result.find("150=0"), std::string::npos);
    EXPECT_NE(result.find("10=123"), std::string::npos);

    // And 43=Y should be present
    EXPECT_NE(result.find("43=Y"), std::string::npos);

    // Message should be exactly 5 bytes longer (43=Y\x01)
    EXPECT_EQ(result.size(), msg.size() + 5);
}

// ─── Integration test: ResendRequest triggers 43=Y ────────────

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

static std::string make_resend_request(const std::string& sender, const std::string& target,
                                        uint64_t msg_seq, uint64_t begin, uint64_t end) {
    std::string msg;
    msg += "8=FIX.4.4"; msg += SOH;
    msg += "35=2"; msg += SOH;
    msg += "49=" + sender; msg += SOH;
    msg += "56=" + target; msg += SOH;
    msg += "34=" + std::to_string(msg_seq); msg += SOH;
    msg += "7=" + std::to_string(begin); msg += SOH;
    msg += "16=" + std::to_string(end); msg += SOH;
    msg += "10=" + gateway::compute_fix_checksum(msg); msg += SOH;
    return msg;
}

static std::string recv_with_timeout(int fd, int timeout_ms = 500) {
    struct timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char buf[8192]{};
    ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return "";
    return std::string(buf, static_cast<size_t>(n));
}

class FixReplayPossDupIntegrationTest : public ::testing::Test {
protected:
    transport::TcpServerSocket* tcp_server_ = nullptr;
    FixTcpAcceptor* acceptor_ = nullptr;
    FixMessageStore* message_store_ = nullptr;
    std::thread server_thread_;
    uint16_t port_ = 0;

    void SetUp() override {
        tcp_server_ = new transport::TcpServerSocket(0);
        ASSERT_TRUE(tcp_server_->listen());
        port_ = tcp_server_->bound_port();
        ASSERT_GT(port_, 0u);

        message_store_ = new FixMessageStore(1000);
        acceptor_ = new FixTcpAcceptor(port_);
        acceptor_->set_server_socket(tcp_server_);
        acceptor_->set_comp_id_resolver([](const std::string&) -> std::optional<engine::ParticipantId> { return engine::ParticipantId{1}; });
        acceptor_->set_message_store(message_store_);
    }

    void start_acceptor() {
        server_thread_ = std::thread([this]() {
            acceptor_->start();
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    int connect_client() {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port_);
        if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            return -1;
        }
        return fd;
    }

    void TearDown() override {
        if (acceptor_) acceptor_->stop();
        if (server_thread_.joinable()) server_thread_.join();
        delete acceptor_;
        delete tcp_server_;
        delete message_store_;
    }
};

TEST_F(FixReplayPossDupIntegrationTest, ReplayedMessageContainsPossDupFlag) {
    FixTcpCallbacks cbs;
    cbs.on_logon = [](engine::SessionId, engine::ParticipantId) {};
    acceptor_->set_callbacks(cbs);

    // Pre-store a message that will be replayed
    std::string stored_msg;
    stored_msg += "8=FIX.4.4"; stored_msg += SOH;
    stored_msg += "35=8"; stored_msg += SOH;
    stored_msg += "49=REPLAY_TGT"; stored_msg += SOH;
    stored_msg += "56=CLIENT"; stored_msg += SOH;
    stored_msg += "34=2"; stored_msg += SOH;
    stored_msg += "10=000"; stored_msg += SOH;
    message_store_->store("REPLAY_TGT", 2, stored_msg);

    start_acceptor();

    int fd = connect_client();
    ASSERT_GE(fd, 0);

    // Logon: client sender=CLIENT, target=REPLAY_TGT
    // This makes session.target_comp_id = REPLAY_TGT (matching store key)
    std::string logon = make_logon("CLIENT", "REPLAY_TGT", 1);
    ::send(fd, logon.c_str(), logon.size(), 0);

    // Read logon ack
    std::string logon_ack = recv_with_timeout(fd);
    EXPECT_FALSE(logon_ack.empty());

    // Send ResendRequest for seq 2
    std::string resend = make_resend_request("CLIENT", "REPLAY_TGT", 2, 2, 2);
    ::send(fd, resend.c_str(), resend.size(), 0);

    // Read the replayed message
    std::string replayed = recv_with_timeout(fd, 1000);
    EXPECT_FALSE(replayed.empty()) << "Should receive a replayed message";

    // Verify 43=Y is present in the replayed message
    EXPECT_NE(replayed.find("43=Y"), std::string::npos)
        << "Replayed message must contain PossDupFlag (43=Y). Got: " << replayed;

    ::close(fd);
}

TEST_F(FixReplayPossDupIntegrationTest, ReplayMixedWithGapFill) {
    FixTcpCallbacks cbs;
    cbs.on_logon = [](engine::SessionId, engine::ParticipantId) {};
    acceptor_->set_callbacks(cbs);

    // Pre-store messages for seq 2 and 4, leave 3 and 5 missing
    auto make_exec = [](uint64_t seq, const std::string& sender) {
        std::string msg;
        msg += "8=FIX.4.4"; msg += SOH;
        msg += "35=8"; msg += SOH;
        msg += "49=" + sender; msg += SOH;
        msg += "56=CLIENT"; msg += SOH;
        msg += "34=" + std::to_string(seq); msg += SOH;
        msg += "10=" + gateway::compute_fix_checksum(msg); msg += SOH;
        return msg;
    };

    message_store_->store("MIX_TGT", 2, make_exec(2, "MIX_TGT"));
    message_store_->store("MIX_TGT", 4, make_exec(4, "MIX_TGT"));

    start_acceptor();

    int fd = connect_client();
    ASSERT_GE(fd, 0);

    std::string logon = make_logon("CLIENT", "MIX_TGT", 1);
    ::send(fd, logon.c_str(), logon.size(), 0);
    std::string logon_ack = recv_with_timeout(fd);
    EXPECT_FALSE(logon_ack.empty());

    // Request range 2-5: seq 2 and 4 should be replayed, 3 and 5 gap-filled
    std::string resend = make_resend_request("CLIENT", "MIX_TGT", 2, 2, 5);
    ::send(fd, resend.c_str(), resend.size(), 0);

    // Read all responses (may come in one or multiple recv calls)
    std::string all_data;
    for (int i = 0; i < 5; ++i) {
        std::string chunk = recv_with_timeout(fd, 200);
        if (chunk.empty()) break;
        all_data += chunk;
    }

    // Verify replayed messages contain 43=Y
    // The replayed messages for seq 2 and 4 should have 43=Y
    // Gap fills (35=4) for seq 3 and 5 should also be present
    EXPECT_NE(all_data.find("43=Y"), std::string::npos)
        << "Replayed messages must contain PossDupFlag (43=Y)";

    // Should contain SequenceReset-GapFill (35=4) for gaps
    EXPECT_NE(all_data.find("35=4"), std::string::npos)
        << "Should contain GapFill messages for missing sequences";

    ::close(fd);
}

}  // namespace
}  // namespace gateway
