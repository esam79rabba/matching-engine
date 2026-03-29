#include "engine/trade_event_bus.h"
#include <gtest/gtest.h>
#include <vector>

using namespace engine;

namespace {

TradeEvent make_event(TradeId tid = 1, Price price = 100, Quantity qty = 10) {
    TradeEvent e{};
    e.trade_id = tid;
    e.instrument_id = 42;
    e.buy_order_id = 100;
    e.sell_order_id = 200;
    e.buyer_id = 1;
    e.seller_id = 2;
    e.price = price;
    e.quantity = qty;
    e.aggressor_side = Side::Buy;
    e.timestamp = 1000;
    return e;
}

}  // namespace

TEST(TradeEventBusTest, SubscribeAndReceive) {
    TradeEventBus bus;
    std::vector<TradeEvent> received;

    bus.subscribe("test-sub", [&](const TradeEvent& e) {
        received.push_back(e);
    });

    auto event = make_event(1, 100, 10);
    bus.publish(event);

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].trade_id, 1u);
    EXPECT_EQ(received[0].price, 100);
    EXPECT_EQ(received[0].quantity, 10u);
    EXPECT_EQ(received[0].buyer_id, 1u);
    EXPECT_EQ(received[0].seller_id, 2u);
}

TEST(TradeEventBusTest, MultipleSubscribers) {
    TradeEventBus bus;
    int count_a = 0, count_b = 0, count_c = 0;

    bus.subscribe("sub-a", [&](const TradeEvent&) { ++count_a; });
    bus.subscribe("sub-b", [&](const TradeEvent&) { ++count_b; });
    bus.subscribe("sub-c", [&](const TradeEvent&) { ++count_c; });

    bus.publish(make_event());

    EXPECT_EQ(count_a, 1);
    EXPECT_EQ(count_b, 1);
    EXPECT_EQ(count_c, 1);
    EXPECT_EQ(bus.subscriber_count(), 3u);
}

TEST(TradeEventBusTest, Unsubscribe) {
    TradeEventBus bus;
    int count = 0;

    auto id = bus.subscribe("removable", [&](const TradeEvent&) { ++count; });
    bus.publish(make_event());
    EXPECT_EQ(count, 1);

    bus.unsubscribe(id);
    bus.publish(make_event());
    EXPECT_EQ(count, 1);  // No longer receives
    EXPECT_EQ(bus.subscriber_count(), 0u);
}

TEST(TradeEventBusTest, EventCountTracked) {
    TradeEventBus bus;
    bus.subscribe("counter", [](const TradeEvent&) {});

    EXPECT_EQ(bus.events_published(), 0u);
    bus.publish(make_event(1));
    bus.publish(make_event(2));
    bus.publish(make_event(3));
    EXPECT_EQ(bus.events_published(), 3u);
}

TEST(TradeEventBusTest, SubscriberNames) {
    TradeEventBus bus;
    bus.subscribe("clearing", [](const TradeEvent&) {});
    bus.subscribe("settlement", [](const TradeEvent&) {});
    bus.subscribe("surveillance", [](const TradeEvent&) {});

    auto names = bus.subscriber_names();
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "clearing");
    EXPECT_EQ(names[1], "settlement");
    EXPECT_EQ(names[2], "surveillance");
}

TEST(TradeEventBusTest, PublishWithNoSubscribers) {
    TradeEventBus bus;
    // Should not crash
    bus.publish(make_event());
    EXPECT_EQ(bus.events_published(), 1u);
    EXPECT_EQ(bus.subscriber_count(), 0u);
}
