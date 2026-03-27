#pragma once

#include "engine/types.h"
#include <cstdint>
#include <vector>
#include <functional>

namespace marketdata {

/// Normalized market data event — the internal representation
/// that the publisher converts into wire-format messages.
///
/// Generic — no asset-class-specific fields. The publisher does not
/// know whether it is publishing equity, derivative, or digital-asset data.
/// Wire-format encoding (SBE multicast, JSON WebSocket, etc.) is a
/// separate concern handled by the transport layer.

struct TopOfBook {
    engine::InstrumentId instrument_id;
    engine::Price        best_bid;
    engine::Quantity     best_bid_qty;
    engine::Price        best_ask;
    engine::Quantity     best_ask_qty;
    engine::Timestamp    timestamp;
};

struct DepthUpdate {
    engine::InstrumentId instrument_id;
    engine::Side         side;
    engine::Price        price;
    engine::Quantity     new_qty;      // New total qty at this level (0 = level removed)
    uint32_t             order_count;  // Number of orders at this level
    engine::Timestamp    timestamp;
};

struct TradeMessage {
    engine::InstrumentId instrument_id;
    engine::Price        price;
    engine::Quantity     quantity;
    engine::Side         aggressor_side;
    engine::TradeId      trade_id;
    engine::Timestamp    timestamp;
};

struct InstrumentStatus {
    engine::InstrumentId instrument_id;
    uint8_t              new_state;     // InstrumentState enum value
    engine::Timestamp    timestamp;
};


/// Abstract publisher output — receives formatted events for delivery.
///
/// In production:
///   - Multicast transport for colocated participants
///   - TCP/WebSocket for WAN participants (Phase 2)
///
/// For M2: a callback-based sink for testing.
class IPublisherSink {
public:
    virtual ~IPublisherSink() = default;

    virtual void publish_top_of_book(const TopOfBook& tob) = 0;
    virtual void publish_depth_update(const DepthUpdate& update) = 0;
    virtual void publish_trade(const TradeMessage& trade) = 0;
    virtual void publish_instrument_status(const InstrumentStatus& status) = 0;
};


/// Market data publisher — converts engine output events into
/// normalized market data messages and delivers them to sinks.
///
/// Architecture:
///   - Receives events from the partition's output bus
///   - Sequences each published message (monotonic per publisher)
///   - Delegates delivery to IPublisherSink implementations
///   - Does NOT directly implement multicast/TCP — that's the transport layer
///
/// Not on the engine hot path, but latency-sensitive.
/// Publication latency target: Profile A < 10 µs from engine event.
class MarketDataPublisher {
public:
    MarketDataPublisher();

    /// Add a delivery sink (e.g., multicast transport, test recorder).
    void add_sink(IPublisherSink* sink);

    /// Called when the engine produces a trade.
    void on_trade(engine::InstrumentId instrument_id,
                  engine::Price price, engine::Quantity qty,
                  engine::Side aggressor_side, engine::TradeId trade_id,
                  engine::Timestamp timestamp);

    /// Called when the book's top-of-book changes.
    void on_top_of_book_change(engine::InstrumentId instrument_id,
                                engine::Price best_bid, engine::Quantity best_bid_qty,
                                engine::Price best_ask, engine::Quantity best_ask_qty,
                                engine::Timestamp timestamp);

    /// Called when a depth level changes (order added/removed/modified).
    void on_depth_change(engine::InstrumentId instrument_id,
                         engine::Side side, engine::Price price,
                         engine::Quantity new_qty, uint32_t order_count,
                         engine::Timestamp timestamp);

    /// Called when an instrument state changes.
    void on_instrument_status(engine::InstrumentId instrument_id,
                               uint8_t new_state, engine::Timestamp timestamp);

    // ─── Sequence and snapshot support ──────────────────────

    /// Current publish sequence number (monotonic).
    [[nodiscard]] uint64_t sequence() const noexcept { return sequence_; }

    /// Snapshot hook: callers can request a full book snapshot.
    /// TODO: implement snapshot delivery for gap recovery.

private:
    std::vector<IPublisherSink*> sinks_;
    uint64_t sequence_ = 0;
};


/// Test sink that records all published events for verification.
class TestPublisherSink : public IPublisherSink {
public:
    std::vector<TopOfBook>        top_of_book_events;
    std::vector<DepthUpdate>      depth_events;
    std::vector<TradeMessage>     trade_events;
    std::vector<InstrumentStatus> status_events;

    void publish_top_of_book(const TopOfBook& tob) override { top_of_book_events.push_back(tob); }
    void publish_depth_update(const DepthUpdate& u) override { depth_events.push_back(u); }
    void publish_trade(const TradeMessage& t) override { trade_events.push_back(t); }
    void publish_instrument_status(const InstrumentStatus& s) override { status_events.push_back(s); }
};

}  // namespace marketdata
