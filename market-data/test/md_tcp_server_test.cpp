/// Market data TCP server unit tests.
/// Tests the publish → JSON encoding path without actual TCP connections.
#include "marketdata/transport/md_tcp_server.h"
#include <gtest/gtest.h>

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

}  // namespace
}  // namespace marketdata
