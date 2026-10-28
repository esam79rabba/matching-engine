/// Session persistence tests for FixTcpAcceptor + FixSessionStore integration.
///
/// Tests verify that session sequence numbers are persisted and restored across
/// acceptor lifecycles.

#include "gateway/tcp/fix_tcp_acceptor.h"
#include "gateway/fix_session_store.h"
#include "transport/tcp_socket.h"

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <cstring>
#include <atomic>
#include <filesystem>
#include <fstream>

namespace gateway {
namespace {

/// Build a minimal FIX Logon message.
static std::string make_logon(const std::string& sender, const std::string& target, int hb = 30) {
    std::string msg;
    msg += "8=FIX.4.4\x01";
    msg += "35=A\x01";
    msg += "49=" + sender + "\x01";
    msg += "56=" + target + "\x01";
    msg += "34=1\x01";
    msg += "108=" + std::to_string(hb) + "\x01";
    msg += "10=" + gateway::compute_fix_checksum(msg) + "\x01";
    return msg;
}

/// Extract tag 34 (MsgSeqNum) from a FIX message string.
static uint64_t extract_seq_num(const std::string& fix_msg) {
    std::string tag = "34=";
    auto pos = fix_msg.find(tag);
    if (pos == std::string::npos) return 0;
    pos += tag.size();
    auto end = fix_msg.find('\x01', pos);
    if (end == std::string::npos) end = fix_msg.size();
    return std::stoull(fix_msg.substr(pos, end - pos));
}

/// Helper: connect plain TCP, send message, receive response.
struct ClientResult {
    std::string response;
    bool connected = false;
};

static ClientResult tcp_send_recv(uint16_t port, const std::string& msg) {
    ClientResult result;
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return result;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return result;
    }
    result.connected = true;

    ::send(fd, msg.c_str(), msg.size(), 0);

    char buf[4096]{};
    ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        result.response = std::string(buf, static_cast<size_t>(n));
    }

    ::close(fd);
    return result;
}

// ─── Test: Logon restores sequence from persisted state ─────

TEST(FixSessionPersistenceTest, LogonRestoresSequence) {
    const std::string store_path = "/tmp/me_fix_persist_test_restore.dat";

    // Pre-populate the store with a session that has advanced sequences
    {
        FixSessionStore store(store_path);
        PersistedSessionState state;
        state.session_id = 42;
        state.sender_comp_id = "PERSIST_CLIENT";
        state.target_comp_id = "PERSIST_SERVER";
        state.inbound_seq = 10;
        state.outbound_seq = 20;
        store.upsert(state);
        ASSERT_TRUE(store.save());
    }

    // Create acceptor with the pre-loaded store
    FixSessionStore store(store_path);
    ASSERT_TRUE(store.load());

    auto* tcp_server = new transport::TcpServerSocket(0);
    ASSERT_TRUE(tcp_server->listen());
    uint16_t port = tcp_server->bound_port();

    FixTcpAcceptor acceptor(port);
    acceptor.set_server_socket(tcp_server);
    acceptor.set_comp_id_resolver([](const std::string&) -> std::optional<engine::ParticipantId> { return engine::ParticipantId{1}; });
    acceptor.set_session_store(&store);

    std::atomic<bool> logon_done{false};
    FixTcpCallbacks cbs;
    cbs.on_logon = [&](engine::SessionId, engine::ParticipantId) {
        logon_done = true;
    };
    acceptor.set_callbacks(cbs);

    std::thread server_thread([&]() { acceptor.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Send Logon with the same sender_comp_id as persisted
    auto result = tcp_send_recv(port, make_logon("PERSIST_CLIENT", "PERSIST_SERVER"));
    ASSERT_TRUE(result.connected);
    ASSERT_FALSE(result.response.empty());

    // The Logon ack's tag 34 should be the persisted outbound_seq (20)
    uint64_t ack_seq = extract_seq_num(result.response);
    EXPECT_EQ(ack_seq, 20u) << "Expected persisted outbound_seq=20 in Logon ack, got " << ack_seq;

    acceptor.stop();
    server_thread.join();
    delete tcp_server;

    // Cleanup
    std::filesystem::remove(store_path);
}

// ─── Test: Shutdown persists session sequences ──────────────

TEST(FixSessionPersistenceTest, ShutdownPersists) {
    const std::string store_path = "/tmp/me_fix_persist_test_shutdown.dat";

    // Clean start
    std::filesystem::remove(store_path);

    FixSessionStore store(store_path);
    store.load();

    auto* tcp_server = new transport::TcpServerSocket(0);
    ASSERT_TRUE(tcp_server->listen());
    uint16_t port = tcp_server->bound_port();

    FixTcpAcceptor acceptor(port);
    acceptor.set_server_socket(tcp_server);
    acceptor.set_comp_id_resolver([](const std::string&) -> std::optional<engine::ParticipantId> { return engine::ParticipantId{1}; });
    acceptor.set_session_store(&store);

    std::atomic<int> msg_count{0};
    FixTcpCallbacks cbs;
    cbs.on_logon = [](engine::SessionId, engine::ParticipantId) {};
    cbs.on_message = [&](engine::SessionId, const std::string&) {
        msg_count++;
    };
    acceptor.set_callbacks(cbs);

    std::thread server_thread([&]() { acceptor.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Logon
    auto logon_result = tcp_send_recv(port, make_logon("SHUTDOWN_CLIENT", "SHUTDOWN_SERVER"));
    ASSERT_TRUE(logon_result.connected);
    ASSERT_FALSE(logon_result.response.empty());
    EXPECT_NE(logon_result.response.find("35=A"), std::string::npos);

    // Stop acceptor — this should persist sessions
    acceptor.stop();
    server_thread.join();
    delete tcp_server;

    // Verify: load the store in a new instance and check sequences
    FixSessionStore store2(store_path);
    ASSERT_TRUE(store2.load());

    auto persisted = store2.get("SHUTDOWN_CLIENT");
    ASSERT_TRUE(persisted.has_value()) << "Session should have been persisted on shutdown";
    // After logon, outbound_seq was incremented (started at 1, sent ack with seq 1, now 2)
    EXPECT_EQ(persisted->outbound_seq, 2u);
    EXPECT_EQ(persisted->sender_comp_id, "SHUTDOWN_CLIENT");

    // Cleanup
    std::filesystem::remove(store_path);
}

// ─── Test: Clean session on first logon ─────────────────────

TEST(FixSessionPersistenceTest, CleanSessionOnFirstLogon) {
    const std::string store_path = "/tmp/me_fix_persist_test_clean.dat";

    // Clean start — no pre-existing state
    std::filesystem::remove(store_path);

    FixSessionStore store(store_path);
    store.load();

    auto* tcp_server = new transport::TcpServerSocket(0);
    ASSERT_TRUE(tcp_server->listen());
    uint16_t port = tcp_server->bound_port();

    FixTcpAcceptor acceptor(port);
    acceptor.set_server_socket(tcp_server);
    acceptor.set_comp_id_resolver([](const std::string&) -> std::optional<engine::ParticipantId> { return engine::ParticipantId{1}; });
    acceptor.set_session_store(&store);

    FixTcpCallbacks cbs;
    cbs.on_logon = [](engine::SessionId, engine::ParticipantId) {};
    acceptor.set_callbacks(cbs);

    std::thread server_thread([&]() { acceptor.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Send Logon — no pre-existing state for this sender
    auto result = tcp_send_recv(port, make_logon("NEW_CLIENT", "NEW_SERVER"));
    ASSERT_TRUE(result.connected);
    ASSERT_FALSE(result.response.empty());

    // The Logon ack should start from seq 1
    uint64_t ack_seq = extract_seq_num(result.response);
    EXPECT_EQ(ack_seq, 1u) << "Expected fresh outbound_seq=1 on first logon, got " << ack_seq;

    acceptor.stop();
    server_thread.join();
    delete tcp_server;

    // Cleanup
    std::filesystem::remove(store_path);
}

}  // namespace
}  // namespace gateway
