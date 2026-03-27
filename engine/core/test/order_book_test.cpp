#include "engine/order_book.h"

#include <gtest/gtest.h>
#include <vector>

namespace engine {
namespace {

// ─── Test helper: simple listener that records events ────────

struct TestListener : OrderBookListener {
    struct Trade {
        OrderId buy_id, sell_id;
        Price price;
        Quantity qty;
        Side aggressor;
    };

    std::vector<const Order*> inserted;
    std::vector<std::pair<const Order*, RejectCode>> cancelled;
    std::vector<Trade> trades;

    void on_order_inserted(const Order& o) override { inserted.push_back(&o); }
    void on_order_cancelled(const Order& o, RejectCode r) override { cancelled.emplace_back(&o, r); }
    void on_order_modified(const Order&) override {}
    void on_trade(OrderId buy, OrderId sell, Price p, Quantity q, Side agg) override {
        trades.push_back({buy, sell, p, q, agg});
    }
};

// ─── Test helper: create a limit order ───────────────────────

Order make_order(OrderId id, Side side, Price price, Quantity qty,
                 uint32_t stp_group = 0) {
    Order o{};
    o.clear();
    o.order_id = id;
    o.client_order_id = id + 1000;
    o.participant_id = 1;
    o.account_id = 1;
    o.session_id = 1;
    o.instrument_id = 100;
    o.side = side;
    o.order_type = OrderType::Limit;
    o.time_in_force = TimeInForce::Day;
    o.price = price;
    o.original_qty = qty;
    o.remaining_qty = qty;
    o.cumulative_filled = 0;
    o.stp_group_id = stp_group;
    o.entry_sequence = id;  // Use id as sequence for simplicity
    return o;
}

// ═══════════════════════════════════════════════════════════════
// Tests
// ═══════════════════════════════════════════════════════════════

TEST(OrderBookTest, EmptyBookHasNoBidOrAsk) {
    OrderBook book(100);
    EXPECT_EQ(book.best_bid(), 0);
    EXPECT_EQ(book.best_ask(), 0);
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(OrderBookTest, InsertSingleBuyOrder) {
    OrderBook book(100);
    TestListener listener;
    book.set_listener(&listener);

    Order buy = make_order(1, Side::Buy, 10000, 100);
    bool filled = book.insert_limit_order(&buy);

    EXPECT_FALSE(filled);                    // Should rest in book (no asks to match)
    EXPECT_EQ(book.best_bid(), 10000);
    EXPECT_EQ(book.best_bid_qty(), 100u);
    EXPECT_EQ(book.best_ask(), 0);           // No sells
    EXPECT_EQ(book.order_count(), 1u);
    EXPECT_EQ(listener.inserted.size(), 1u);
}

TEST(OrderBookTest, InsertSingleSellOrder) {
    OrderBook book(100);

    Order sell = make_order(1, Side::Sell, 10050, 200);
    book.insert_limit_order(&sell);

    EXPECT_EQ(book.best_ask(), 10050);
    EXPECT_EQ(book.best_ask_qty(), 200u);
    EXPECT_EQ(book.best_bid(), 0);
}

TEST(OrderBookTest, CancelOrderRemovesFromBook) {
    OrderBook book(100);
    TestListener listener;
    book.set_listener(&listener);

    Order buy = make_order(1, Side::Buy, 10000, 100);
    book.insert_limit_order(&buy);
    EXPECT_EQ(book.order_count(), 1u);

    bool cancelled = book.cancel_order(1);
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(book.order_count(), 0u);
    EXPECT_EQ(book.best_bid(), 0);
    EXPECT_EQ(listener.cancelled.size(), 1u);
}

TEST(OrderBookTest, CancelNonExistentOrderReturnsFalse) {
    OrderBook book(100);
    EXPECT_FALSE(book.cancel_order(999));
}

TEST(OrderBookTest, MatchCrossingOrders) {
    OrderBook book(100);
    TestListener listener;
    book.set_listener(&listener);

    // Resting sell at 10050
    Order sell = make_order(1, Side::Sell, 10050, 100);
    book.insert_limit_order(&sell);

    // Aggressive buy at 10050 — should match
    Order buy = make_order(2, Side::Buy, 10050, 100);
    bool filled = book.insert_limit_order(&buy);

    EXPECT_TRUE(filled);                     // Fully filled
    EXPECT_EQ(book.order_count(), 0u);       // Both orders consumed
    EXPECT_EQ(listener.trades.size(), 1u);
    EXPECT_EQ(listener.trades[0].price, 10050);
    EXPECT_EQ(listener.trades[0].qty, 100u);
    EXPECT_EQ(listener.trades[0].aggressor, Side::Buy);
}

TEST(OrderBookTest, PartialFillLeavesResidual) {
    OrderBook book(100);
    TestListener listener;
    book.set_listener(&listener);

    // Resting sell of 50
    Order sell = make_order(1, Side::Sell, 10050, 50);
    book.insert_limit_order(&sell);

    // Aggressive buy of 100 — matches 50, rests 50
    Order buy = make_order(2, Side::Buy, 10050, 100);
    bool filled = book.insert_limit_order(&buy);

    EXPECT_FALSE(filled);                    // Residual resting
    EXPECT_EQ(buy.remaining_qty, 50u);
    EXPECT_EQ(buy.cumulative_filled, 50u);
    EXPECT_EQ(book.best_bid(), 10050);       // Residual resting as bid
    EXPECT_EQ(book.best_bid_qty(), 50u);
    EXPECT_EQ(book.best_ask(), 0);           // Sell fully consumed
    EXPECT_EQ(listener.trades.size(), 1u);
}

TEST(OrderBookTest, PriceTimePriority) {
    OrderBook book(100);
    TestListener listener;
    book.set_listener(&listener);

    // Two sells at same price — order 1 has priority (lower sequence)
    Order sell1 = make_order(1, Side::Sell, 10050, 100);
    Order sell2 = make_order(2, Side::Sell, 10050, 100);
    book.insert_limit_order(&sell1);
    book.insert_limit_order(&sell2);

    // Aggressive buy for 50 — should match sell1 first (FIFO)
    Order buy = make_order(3, Side::Buy, 10050, 50);
    book.insert_limit_order(&buy);

    EXPECT_EQ(listener.trades.size(), 1u);
    EXPECT_EQ(listener.trades[0].sell_id, 1u);  // sell1 matched first
    EXPECT_EQ(sell1.remaining_qty, 50u);         // sell1 partially filled
    EXPECT_EQ(sell2.remaining_qty, 100u);         // sell2 untouched
}

TEST(OrderBookTest, MultiplePriceLevelSweep) {
    OrderBook book(100);
    TestListener listener;
    book.set_listener(&listener);

    // Three sells at different prices
    Order sell1 = make_order(1, Side::Sell, 10050, 30);
    Order sell2 = make_order(2, Side::Sell, 10060, 30);
    Order sell3 = make_order(3, Side::Sell, 10070, 30);
    book.insert_limit_order(&sell1);
    book.insert_limit_order(&sell2);
    book.insert_limit_order(&sell3);

    // Aggressive buy at 10070 for 70 — sweeps through 10050 and 10060, partial at 10070
    Order buy = make_order(4, Side::Buy, 10070, 70);
    book.insert_limit_order(&buy);

    EXPECT_EQ(listener.trades.size(), 3u);
    EXPECT_EQ(listener.trades[0].price, 10050);  // Best price first
    EXPECT_EQ(listener.trades[0].qty, 30u);
    EXPECT_EQ(listener.trades[1].price, 10060);
    EXPECT_EQ(listener.trades[1].qty, 30u);
    EXPECT_EQ(listener.trades[2].price, 10070);
    EXPECT_EQ(listener.trades[2].qty, 10u);       // Partial fill at worst level
}

TEST(OrderBookTest, STPCancelNewestBlocksMatch) {
    OrderBook book(100);
    TestListener listener;
    book.set_listener(&listener);

    // Resting sell with STP group 42
    Order sell = make_order(1, Side::Sell, 10050, 100);
    sell.stp_group_id = 42;
    book.insert_limit_order(&sell);

    // Aggressive buy with same STP group — should be cancelled (cancel-newest)
    Order buy = make_order(2, Side::Buy, 10050, 100);
    buy.stp_group_id = 42;
    bool filled = book.insert_limit_order(&buy);

    EXPECT_TRUE(filled);                     // "Filled" = consumed (cancelled in this case)
    EXPECT_EQ(buy.remaining_qty, 0u);        // Aggressive cancelled
    EXPECT_EQ(listener.trades.size(), 0u);   // No trade generated
    EXPECT_EQ(listener.cancelled.size(), 1u);
    EXPECT_EQ(listener.cancelled[0].second, RejectCode::STPCancelled);
    EXPECT_EQ(sell.remaining_qty, 100u);     // Passive untouched
}

TEST(OrderBookTest, NoCrossingWhenPricesDontMeet) {
    OrderBook book(100);

    Order sell = make_order(1, Side::Sell, 10050, 100);
    book.insert_limit_order(&sell);

    // Buy at 10040 — below ask, no crossing
    Order buy = make_order(2, Side::Buy, 10040, 100);
    bool filled = book.insert_limit_order(&buy);

    EXPECT_FALSE(filled);
    EXPECT_EQ(book.order_count(), 2u);
    EXPECT_EQ(book.best_bid(), 10040);
    EXPECT_EQ(book.best_ask(), 10050);
}

TEST(OrderBookTest, FindOrderReturnsCorrectPointer) {
    OrderBook book(100);

    Order buy = make_order(1, Side::Buy, 10000, 100);
    book.insert_limit_order(&buy);

    Order* found = book.find_order(1);
    EXPECT_NE(found, nullptr);
    EXPECT_EQ(found->order_id, 1u);

    EXPECT_EQ(book.find_order(999), nullptr);
}

}  // namespace
}  // namespace engine
