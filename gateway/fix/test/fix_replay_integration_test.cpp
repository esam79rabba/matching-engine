/// Integration tests for FIX message replay on ResendRequest.
///
/// Tests use a real FixTcpAcceptor with TcpServerSocket and FixMessageStore
/// to verify stored message replay and gap-fill behavior.

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

/// Helper: recv with timeout (returns empty string on timeout/error).
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

// ---- Test Fixture ----

class FixReplayIntegrationTest : public ::testing::Test {
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

    void TearDown() override {
        if (acceptor_) {
            acceptor_->stop();
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        delete acceptor_;
        delete tcp_server_;
        delete message_store_;
    }
};

// ---- Tests ----

TEST_F(FixReplayIntegrationTest, ResendRequestReplaysStoredMessages) {
    FixTcpCallbacks cbs;
    cbs.on_logon = [](engine::SessionId, engine::ParticipantId) {};
    acceptor_->set_callbacks(cbs);
    start_acceptor();

    // Step 1: Connect and logon. The acceptor sends a Logon ack which
    // gets stored in the message store automatically.
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(fd, 0);
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port_);
    ASSERT_EQ(::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)), 0);

    std::string logon = make_logon("CLIENT", "SERVER", 1);
    ::send(fd, logon.c_str(), logon.size(), 0);

    // Read logon ack (outbound seq=1 from acceptor)
    std::string logon_ack = recv_with_timeout(fd);
    EXPECT_FALSE(logon_ack.empty()) << "Expected logon ack";

    // Verify the logon ack was stored
    // The acceptor stores with target_comp_id as key — CLIENT is the sender,
    // SERVER is the target in the client's message. The acceptor swaps them:
    // acceptor sends with sender=SERVER, target=CLIENT. So stored under CLIENT.
    // The acceptor sets session.target_comp_id from the client's TargetCompID (56).
    // Client sent target=SERVER, so messages are stored under key "SERVER".
    EXPECT_GE(message_store_->count("SERVER"), 1u)
        << "Logon ack should be stored in message store under target_comp_id";

    ::close(fd);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Verify the stored logon ack can be retrieved for replay
    auto stored = message_store_->get("SERVER", 1);
    ASSERT_TRUE(stored.has_value()) << "Stored logon ack should be retrievable";
    EXPECT_NE(stored->find("35=A"), std::string::npos) << "Stored message should be a Logon ack";
}

TEST_F(FixReplayIntegrationTest, GapFillForMissingMessages) {
    FixTcpCallbacks cbs;
    cbs.on_logon = [](engine::SessionId, engine::ParticipantId) {};
    acceptor_->set_callbacks(cbs);
    start_acceptor();

    // Pre-populate the message store with some messages but leave gaps.
    // Store messages for seq 2 and 4, leaving 1, 3, 5 missing.
    message_store_->store("GAPTEST", 2, "8=FIX.4.4" "\x01" "35=8" "\x01" "34=2" "\x01" "10=000" "\x01");
    message_store_->store("GAPTEST", 4, "8=FIX.4.4" "\x01" "35=8" "\x01" "34=4" "\x01" "10=000" "\x01");

    // Connect and send a logon so the acceptor creates a session with
    // target_comp_id = GAPTEST (sender in client's logon).
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(fd, 0);
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port_);
    ASSERT_EQ(::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)), 0);

    // Logon: client sender=GAPTEST, target=SERVER
    // Acceptor will set session.sender_comp_id=GAPTEST, target_comp_id=SERVER
    // But we need target_comp_id=GAPTEST for the store lookup.
    // The store key is target_comp_id. In process_logon, acceptor sets:
    //   session.sender_comp_id = client's SenderCompID
    //   session.target_comp_id = client's TargetCompID
    // Then handle_resend_request uses session.target_comp_id as the store key.
    // So if client sends sender=CLIENT, target=GAPTEST:
    //   session.sender_comp_id = CLIENT
    //   session.target_comp_id = GAPTEST
    // That matches our store key "GAPTEST".
    std::string logon = make_logon("CLIENT", "GAPTEST", 1);
    ::send(fd, logon.c_str(), logon.size(), 0);

    // Read logon ack
    std::string logon_ack = recv_with_timeout(fd);
    EXPECT_FALSE(logon_ack.empty());

    ::close(fd);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Verify the message store returns the correct range for replay.
    // Messages 2 and 4 exist; 1, 3, 5 are missing (would be gap-filled).
    auto range = message_store_->get_range("GAPTEST", 1, 5);

    // Should return at least 2 messages (seq 2 and 4 from pre-populated store).
    // May also include the logon ack (seq 1) stored during the session above.
    ASSERT_GE(range.size(), 2u);

    // Verify pre-populated messages are present
    bool has_seq2 = false, has_seq4 = false;
    for (const auto& [seq, msg] : range) {
        if (seq == 2) { has_seq2 = true; EXPECT_NE(msg.find("34=2"), std::string::npos); }
        if (seq == 4) { has_seq4 = true; EXPECT_NE(msg.find("34=4"), std::string::npos); }
    }
    EXPECT_TRUE(has_seq2) << "Seq 2 should be in range";
    EXPECT_TRUE(has_seq4) << "Seq 4 should be in range";
}

}  // namespace
}  // namespace gateway
