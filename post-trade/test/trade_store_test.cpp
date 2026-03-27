#include "posttrade/trade_store.h"

#include <gtest/gtest.h>

namespace {

using posttrade::OrderRecord;
using posttrade::TradeRecord;
using posttrade::TradeStore;

TradeRecord make_trade(uint64_t id, uint32_t buyer, uint32_t seller,
                       uint32_t instrument, int64_t price, uint64_t qty,
                       uint64_t ts) {
    TradeRecord r;
    r.trade_id       = id;
    r.buy_order_id   = id * 10;
    r.sell_order_id  = id * 10 + 1;
    r.buyer_id       = buyer;
    r.seller_id      = seller;
    r.instrument_id  = instrument;
    r.price          = price;
    r.quantity        = qty;
    r.aggressor_side = engine::Side::Buy;
    r.timestamp      = ts;
    return r;
}

TEST(TradeStore, RecordAndQueryTrade) {
    TradeStore store;
    auto t = make_trade(1, 100, 200, 1, 5000, 10, 1000);
    store.record_trade(t);

    EXPECT_EQ(store.trade_count(), 1u);

    auto results = store.query_by_participant(100);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].trade_id, 1u);
    EXPECT_EQ(results[0].price, 5000);
    EXPECT_EQ(results[0].quantity, 10u);
}

TEST(TradeStore, QueryByParticipant) {
    TradeStore store;
    store.record_trade(make_trade(1, 100, 200, 1, 5000, 10, 1000));
    store.record_trade(make_trade(2, 300, 100, 1, 5100, 5, 2000));
    store.record_trade(make_trade(3, 400, 500, 2, 6000, 20, 3000));

    // Participant 100 is buyer in trade 1, seller in trade 2
    auto results = store.query_by_participant(100);
    EXPECT_EQ(results.size(), 2u);

    // With limit = 1, should get most recent first (reverse order)
    auto limited = store.query_by_participant(100, 1);
    ASSERT_EQ(limited.size(), 1u);
    EXPECT_EQ(limited[0].trade_id, 2u);

    // Participant 400 is buyer in trade 3 only
    auto p400 = store.query_by_participant(400);
    EXPECT_EQ(p400.size(), 1u);
}

TEST(TradeStore, QueryByInstrument) {
    TradeStore store;
    store.record_trade(make_trade(1, 100, 200, 1, 5000, 10, 1000));
    store.record_trade(make_trade(2, 300, 400, 2, 6000, 20, 2000));
    store.record_trade(make_trade(3, 500, 600, 1, 5100, 5, 3000));

    auto inst1 = store.query_by_instrument(1);
    EXPECT_EQ(inst1.size(), 2u);

    auto inst2 = store.query_by_instrument(2);
    EXPECT_EQ(inst2.size(), 1u);

    auto inst3 = store.query_by_instrument(99);
    EXPECT_TRUE(inst3.empty());
}

TEST(TradeStore, QueryByTimeRange) {
    TradeStore store;
    store.record_trade(make_trade(1, 100, 200, 1, 5000, 10, 1000));
    store.record_trade(make_trade(2, 300, 400, 1, 5100, 5, 2000));
    store.record_trade(make_trade(3, 500, 600, 1, 5200, 15, 3000));
    store.record_trade(make_trade(4, 700, 800, 1, 5300, 8, 4000));

    auto range = store.query_by_time_range(1500, 3500);
    EXPECT_EQ(range.size(), 2u);

    auto limited = store.query_by_time_range(1000, 4000, 2);
    EXPECT_EQ(limited.size(), 2u);

    auto empty = store.query_by_time_range(5000, 6000);
    EXPECT_TRUE(empty.empty());
}

TEST(TradeStore, RecordAndQueryOrder) {
    TradeStore store;

    OrderRecord o1;
    o1.event          = OrderRecord::Event::New;
    o1.order_id       = 1;
    o1.participant_id = 100;
    o1.instrument_id  = 1;
    o1.side           = engine::Side::Buy;
    o1.price          = 5000;
    o1.quantity       = 10;
    o1.timestamp      = 1000;

    OrderRecord o2;
    o2.event          = OrderRecord::Event::Fill;
    o2.order_id       = 1;
    o2.participant_id = 100;
    o2.instrument_id  = 1;
    o2.side           = engine::Side::Buy;
    o2.price          = 5000;
    o2.quantity       = 10;
    o2.timestamp      = 2000;

    OrderRecord o3;
    o3.event          = OrderRecord::Event::New;
    o3.order_id       = 2;
    o3.participant_id = 200;
    o3.instrument_id  = 2;
    o3.side           = engine::Side::Sell;
    o3.price          = 6000;
    o3.quantity       = 5;
    o3.timestamp      = 3000;

    store.record_order(o1);
    store.record_order(o2);
    store.record_order(o3);

    EXPECT_EQ(store.order_count(), 3u);

    auto p100 = store.query_orders_by_participant(100);
    EXPECT_EQ(p100.size(), 2u);
    // Most recent first
    EXPECT_EQ(p100[0].timestamp, 2000u);

    auto limited = store.query_orders_by_participant(100, 1);
    ASSERT_EQ(limited.size(), 1u);
    EXPECT_EQ(limited[0].event, OrderRecord::Event::Fill);

    auto p200 = store.query_orders_by_participant(200);
    EXPECT_EQ(p200.size(), 1u);
}

TEST(TradeStore, EmptyStoreReturnsEmpty) {
    TradeStore store;

    EXPECT_EQ(store.trade_count(), 0u);
    EXPECT_EQ(store.order_count(), 0u);
    EXPECT_TRUE(store.query_by_participant(100).empty());
    EXPECT_TRUE(store.query_by_instrument(1).empty());
    EXPECT_TRUE(store.query_by_time_range(0, 99999).empty());
    EXPECT_TRUE(store.query_orders_by_participant(100).empty());
}

}  // namespace
