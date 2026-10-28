/// Verifies FixTcpAcceptor resolves participant_id from SenderCompID (tag 49)
/// and rejects Logons from unknown CompIDs.

#include "gateway/tcp/fix_tcp_acceptor.h"
#include "transport/tcp_socket.h"

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <thread>

namespace gateway {
namespace {

constexpr char SOH = '\x01';

std::string make_logon(const std::string& sender, const std::string& target) {
    const std::string body = std::string("8=FIX.4.4") + SOH + "35=A" + SOH + "49=" + sender + SOH +
           "56=" + target + SOH + "34=1" + SOH + "108=30" + SOH;
    return body + "10=" + compute_fix_checksum(body) + SOH;
}

class CompIdResolverTest : public ::testing::Test {
protected:
    transport::TcpServerSocket* server_ = nullptr;
    FixTcpAcceptor* acceptor_ = nullptr;
    std::thread thread_;
    uint16_t port_ = 0;
    std::atomic<int> logon_count_{0};
    std::atomic<uint64_t> logon_pid_{0};

    void SetUp() override {
        server_ = new transport::TcpServerSocket(0);
        ASSERT_TRUE(server_->listen());
        port_ = server_->bound_port();
        acceptor_ = new FixTcpAcceptor(port_);
        acceptor_->set_server_socket(server_);
        acceptor_->set_comp_id_resolver(
            [](const std::string& id) -> std::optional<engine::ParticipantId> {
                if (id == "PARTICIPANT_1") return engine::ParticipantId{42};
                return std::nullopt;
            });
        FixTcpCallbacks cbs;
        cbs.on_logon = [this](engine::SessionId, engine::ParticipantId pid) {
            logon_pid_ = static_cast<uint64_t>(pid);
            ++logon_count_;
        };
        acceptor_->set_callbacks(cbs);
        thread_ = std::thread([this] { acceptor_->start(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void TearDown() override {
        acceptor_->stop();
        if (thread_.joinable()) thread_.join();
        delete acceptor_;
        delete server_;
    }

    /// Send a Logon, return everything the acceptor sends back before closing/timeout.
    std::string logon_and_read(const std::string& sender) {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_GE(fd, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        EXPECT_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
        timeval tv{2, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        std::string msg = make_logon(sender, "VENUE");
        ::send(fd, msg.data(), msg.size(), 0);
        char buf[1024];
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        ::close(fd);
        return n > 0 ? std::string(buf, static_cast<size_t>(n)) : std::string();
    }
};

TEST_F(CompIdResolverTest, KnownSenderCompIdResolvesParticipantId) {
    std::string reply = logon_and_read("PARTICIPANT_1");
    EXPECT_NE(reply.find("35=A"), std::string::npos) << reply;
    EXPECT_EQ(logon_count_.load(), 1);
    EXPECT_EQ(logon_pid_.load(), 42u);
}

TEST_F(CompIdResolverTest, UnknownSenderCompIdRejectedWithLogout) {
    std::string reply = logon_and_read("UNKNOWN");
    EXPECT_NE(reply.find("35=5"), std::string::npos) << reply;
    EXPECT_NE(reply.find("58=Unknown SenderCompID"), std::string::npos) << reply;
    EXPECT_EQ(reply.find("35=A"), std::string::npos);
    EXPECT_EQ(logon_count_.load(), 0);
    EXPECT_EQ(acceptor_->session_count(), 0u);
}

TEST(CompIdResolverNoResolverTest, FailsClosedWithoutResolver) {
    auto* server = new transport::TcpServerSocket(0);
    ASSERT_TRUE(server->listen());
    uint16_t port = server->bound_port();
    FixTcpAcceptor acceptor(port);
    acceptor.set_server_socket(server);
    std::atomic<int> logons{0};
    FixTcpCallbacks cbs;
    cbs.on_logon = [&](engine::SessionId, engine::ParticipantId) { ++logons; };
    acceptor.set_callbacks(cbs);
    std::thread t([&] { acceptor.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    ASSERT_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    timeval tv{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    std::string msg = make_logon("PARTICIPANT_1", "VENUE");
    ::send(fd, msg.data(), msg.size(), 0);
    char buf[512];
    ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    ::close(fd);
    std::string reply = n > 0 ? std::string(buf, static_cast<size_t>(n)) : "";
    EXPECT_NE(reply.find("35=5"), std::string::npos) << reply;
    EXPECT_EQ(logons.load(), 0);

    acceptor.stop();
    t.join();
    delete server;
}

}  // namespace
}  // namespace gateway
