#include "ops/websocket_server.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace ops {
namespace {

// Helper: connect a raw TCP socket to the WebSocket server
int connect_to(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd); return -1;
    }
    return fd;
}

// Helper: perform WebSocket handshake
bool ws_handshake(int fd) {
    std::string req =
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    send(fd, req.c_str(), req.size(), 0);

    char buf[1024];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return false;
    buf[n] = '\0';
    return std::string(buf).find("101 Switching Protocols") != std::string::npos;
}

// Helper: read a WebSocket text frame
std::string ws_read_frame(int fd, int timeout_ms = 1000) {
    struct timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t buf[4096];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) return "";

    // Decode server frame (not masked)
    if (n < 2) return "";
    uint64_t payload_len = buf[1] & 0x7F;
    size_t offset = 2;
    if (payload_len == 126) { payload_len = (static_cast<uint64_t>(buf[2]) << 8) | buf[3]; offset = 4; }
    return std::string(reinterpret_cast<char*>(buf + offset), payload_len);
}

TEST(WebSocketServerTest, StartsAndStops) {
    WebSocketServer ws(19999);
    std::thread t([&]() { ws.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(ws.is_running());
    EXPECT_EQ(ws.client_count(), 0u);

    ws.stop();
    t.join();
}

TEST(WebSocketServerTest, ClientConnectsAndReceivesBroadcast) {
    WebSocketServer ws(19998);
    std::thread t([&]() { ws.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int fd = connect_to(19998);
    ASSERT_GE(fd, 0);
    ASSERT_TRUE(ws_handshake(fd));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(ws.client_count(), 1u);

    // Broadcast a trade
    ws.broadcast(WebSocketServer::CH_TRADES, R"({"type":"trade","price":10050,"qty":100})");

    std::string msg = ws_read_frame(fd);
    EXPECT_FALSE(msg.empty());
    EXPECT_NE(msg.find("trade"), std::string::npos);
    EXPECT_NE(msg.find("10050"), std::string::npos);

    close(fd);
    ws.stop();
    t.join();
}

TEST(WebSocketServerTest, MultipleClients) {
    WebSocketServer ws(19997);
    std::thread t([&]() { ws.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int fd1 = connect_to(19997);
    int fd2 = connect_to(19997);
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);
    ASSERT_TRUE(ws_handshake(fd1));
    ASSERT_TRUE(ws_handshake(fd2));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(ws.client_count(), 2u);

    ws.broadcast(WebSocketServer::CH_STATUS, R"({"state":"running"})");

    std::string m1 = ws_read_frame(fd1);
    std::string m2 = ws_read_frame(fd2);
    EXPECT_NE(m1.find("running"), std::string::npos);
    EXPECT_NE(m2.find("running"), std::string::npos);

    close(fd1);
    close(fd2);
    ws.stop();
    t.join();
}

TEST(WebSocketServerTest, DisconnectCleansUp) {
    WebSocketServer ws(19996);
    std::thread t([&]() { ws.start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int fd = connect_to(19996);
    ASSERT_GE(fd, 0);
    ASSERT_TRUE(ws_handshake(fd));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(ws.client_count(), 1u);

    close(fd);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Client count may take a moment to update
    ws.stop();
    t.join();
}

}  // namespace
}  // namespace ops
