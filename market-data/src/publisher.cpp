#include "marketdata/publisher.h"

namespace marketdata {

MarketDataPublisher::MarketDataPublisher() = default;

void MarketDataPublisher::add_sink(IPublisherSink* sink) {
    if (sink) sinks_.push_back(sink);
}

void MarketDataPublisher::on_trade(
        engine::InstrumentId instrument_id,
        engine::Price price, engine::Quantity qty,
        engine::Side aggressor_side, engine::TradeId trade_id,
        engine::Timestamp timestamp) {
    ++sequence_;

    TradeMessage msg{};
    msg.instrument_id = instrument_id;
    msg.price = price;
    msg.quantity = qty;
    msg.aggressor_side = aggressor_side;
    msg.trade_id = trade_id;
    msg.timestamp = timestamp;

    for (auto* sink : sinks_) {
        sink->publish_trade(msg);
    }
}

void MarketDataPublisher::on_top_of_book_change(
        engine::InstrumentId instrument_id,
        engine::Price best_bid, engine::Quantity best_bid_qty,
        engine::Price best_ask, engine::Quantity best_ask_qty,
        engine::Timestamp timestamp) {
    ++sequence_;

    TopOfBook tob{};
    tob.instrument_id = instrument_id;
    tob.best_bid = best_bid;
    tob.best_bid_qty = best_bid_qty;
    tob.best_ask = best_ask;
    tob.best_ask_qty = best_ask_qty;
    tob.timestamp = timestamp;

    for (auto* sink : sinks_) {
        sink->publish_top_of_book(tob);
    }
}

void MarketDataPublisher::on_depth_change(
        engine::InstrumentId instrument_id,
        engine::Side side, engine::Price price,
        engine::Quantity new_qty, uint32_t order_count,
        engine::Timestamp timestamp) {
    ++sequence_;

    DepthUpdate update{};
    update.instrument_id = instrument_id;
    update.side = side;
    update.price = price;
    update.new_qty = new_qty;
    update.order_count = order_count;
    update.timestamp = timestamp;

    for (auto* sink : sinks_) {
        sink->publish_depth_update(update);
    }
}

void MarketDataPublisher::on_instrument_status(
        engine::InstrumentId instrument_id,
        uint8_t new_state, engine::Timestamp timestamp) {
    ++sequence_;

    InstrumentStatus status{};
    status.instrument_id = instrument_id;
    status.new_state = new_state;
    status.timestamp = timestamp;

    for (auto* sink : sinks_) {
        sink->publish_instrument_status(status);
    }
}

}  // namespace marketdata
