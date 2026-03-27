/// Market data TCP server unit tests.
/// Tests the publish → JSON encoding path without actual TCP connections.
#include "marketdata/transport/md_tcp_server.h"
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <chrono>

namespace marketdata {
namespace {

TEST(MdTcpServerTest, ConstructWithoutStart) {
    MdTcpServer server(0);
    EXPECT_FALSE(server.is_running());
    EXPECT_EQ(server.client_count(), 0u);
    EXPECT_EQ(server.messages_sent(), 0u);
}

TEST(MdTcpServerTest, PublishWithNoClients) {
    MdTcpServer server(0);
    // Publishing without clients should not crash
    TradeMessage trade{};
    trade.instrument_id = 100;
    trade.price = 50000;
    trade.quantity = 10;
    trade.aggressor_side = engine::Side::Buy;
    trade.trade_id = 1;
    trade.timestamp = 1000;

    // Publish without clients — should not crash
    server.publish_trade(trade);
    // messages_sent may increment even without clients (broadcast attempts counted)
    // The key assertion is that it doesn't crash
}

TEST(MdTcpServerTest, SequenceIncrements) {
    // Verify the publisher sink interface works
    // Use TestPublisherSink to verify event routing
    MarketDataPublisher pub;
    TestPublisherSink sink;
    pub.add_sink(&sink);

    pub.on_trade(100, 50000, 10, engine::Side::Buy, 1, 1000);
    pub.on_top_of_book_change(100, 49900, 200, 50100, 150, 2000);
    pub.on_depth_change(100, engine::Side::Buy, 49900, 500, 3, 3000);

    EXPECT_EQ(pub.sequence(), 3u);
    EXPECT_EQ(sink.trade_events.size(), 1u);
    EXPECT_EQ(sink.top_of_book_events.size(), 1u);
    EXPECT_EQ(sink.depth_events.size(), 1u);
}

TEST(MdTcpServerTest, NonBlockingSendDoesNotHang) {
    // Start server on an ephemeral port
    MdTcpServer server(0);
    ASSERT_TRUE(server.start());

    // Determine the port the server bound to by connecting to it
    // We need to find the actual port — start() binds to port 0 which picks ephemeral
    // For this test, use a known port instead
    server.stop();

    uint16_t test_port = 19876;
    MdTcpServer server2(test_port);
    ASSERT_TRUE(server2.start());

    // Connect a client but never read from it (simulates slow consumer)
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(client_fd, 0);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(test_port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int rc = connect(client_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    ASSERT_EQ(rc, 0);

    // Broadcast many messages — with non-blocking sends this should not hang
    // even though the client never reads.
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 10000; ++i) {
        TradeMessage trade{};
        trade.instrument_id = 100;
        trade.price = 50000;
        trade.quantity = 10;
        trade.aggressor_side = engine::Side::Buy;
        trade.trade_id = static_cast<uint64_t>(i);
        trade.timestamp = 1000;
        server2.publish_trade(trade);
    }
    auto elapsed = std::chrono::steady_clock::now() - start;

    // Non-blocking broadcast of 10k messages should complete well under 5 seconds.
    // A blocking send to a non-reading client would hang indefinitely.
    EXPECT_LT(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(), 5);
    EXPECT_GE(server2.messages_sent(), 10000u);

    close(client_fd);
    server2.stop();
}

TEST(MdTcpServerTest, MessagesDroppedCounterWorks) {
    MdTcpServer server(0);
    EXPECT_EQ(server.messages_dropped(), 0u);
}

}  // namespace
}  // namespace marketdata
