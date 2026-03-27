#include "marketdata/publisher.h"
#include <gtest/gtest.h>

namespace marketdata {
namespace {

TEST(PublisherTest, InitialSequenceIsZero) {
    MarketDataPublisher pub;
    EXPECT_EQ(pub.sequence(), 0u);
}

TEST(PublisherTest, TradePublishIncreasesSequence) {
    MarketDataPublisher pub;
    TestPublisherSink sink;
    pub.add_sink(&sink);

    pub.on_trade(100, 50000, 10, engine::Side::Buy, 1, 1000);
    EXPECT_EQ(pub.sequence(), 1u);
    ASSERT_EQ(sink.trade_events.size(), 1u);
    EXPECT_EQ(sink.trade_events[0].instrument_id, 100u);
    EXPECT_EQ(sink.trade_events[0].price, 50000);
    EXPECT_EQ(sink.trade_events[0].quantity, 10u);
    EXPECT_EQ(sink.trade_events[0].aggressor_side, engine::Side::Buy);
}

TEST(PublisherTest, TopOfBookPublish) {
    MarketDataPublisher pub;
    TestPublisherSink sink;
    pub.add_sink(&sink);

    pub.on_top_of_book_change(100, 49900, 200, 50100, 150, 2000);
    ASSERT_EQ(sink.top_of_book_events.size(), 1u);
    EXPECT_EQ(sink.top_of_book_events[0].best_bid, 49900);
    EXPECT_EQ(sink.top_of_book_events[0].best_ask, 50100);
}

TEST(PublisherTest, DepthUpdatePublish) {
    MarketDataPublisher pub;
    TestPublisherSink sink;
    pub.add_sink(&sink);

    pub.on_depth_change(100, engine::Side::Buy, 49900, 500, 3, 3000);
    ASSERT_EQ(sink.depth_events.size(), 1u);
    EXPECT_EQ(sink.depth_events[0].price, 49900);
    EXPECT_EQ(sink.depth_events[0].new_qty, 500u);
}

TEST(PublisherTest, MultipleSinksReceiveEvents) {
    MarketDataPublisher pub;
    TestPublisherSink sink1, sink2;
    pub.add_sink(&sink1);
    pub.add_sink(&sink2);

    pub.on_trade(100, 50000, 10, engine::Side::Sell, 1, 4000);
    EXPECT_EQ(sink1.trade_events.size(), 1u);
    EXPECT_EQ(sink2.trade_events.size(), 1u);
}

TEST(PublisherTest, InstrumentStatusPublish) {
    MarketDataPublisher pub;
    TestPublisherSink sink;
    pub.add_sink(&sink);

    pub.on_instrument_status(100, 3, 5000);  // 3 = Continuous
    ASSERT_EQ(sink.status_events.size(), 1u);
    EXPECT_EQ(sink.status_events[0].new_state, 3);
}

TEST(PublisherTest, SequenceMonotonicallyIncreases) {
    MarketDataPublisher pub;
    TestPublisherSink sink;
    pub.add_sink(&sink);

    pub.on_trade(100, 50000, 10, engine::Side::Buy, 1, 1000);
    pub.on_top_of_book_change(100, 49900, 200, 50100, 150, 2000);
    pub.on_depth_change(100, engine::Side::Sell, 50100, 300, 2, 3000);

    EXPECT_EQ(pub.sequence(), 3u);
}

}  // namespace
}  // namespace marketdata
