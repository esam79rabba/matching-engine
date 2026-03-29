#pragma once

#include <engine/types.h>

#include <cstddef>
#include <mutex>
#include <vector>

namespace posttrade {

// ─── Records ─────────────────────────────────────────────────

struct TradeRecord {
    engine::TradeId       trade_id{};
    engine::OrderId       buy_order_id{};
    engine::OrderId       sell_order_id{};
    engine::ParticipantId buyer_id{};
    engine::ParticipantId seller_id{};
    engine::InstrumentId  instrument_id{};
    engine::Price         price{};
    engine::Quantity      quantity{};
    engine::Side          aggressor_side{};
    engine::Timestamp     timestamp{};
    int64_t               buyer_fee  = 0;
    int64_t               seller_fee = 0;
};

struct OrderRecord {
    enum class Event : uint8_t { New, Cancel, Modify, Fill, Reject };

    Event                 event{};
    engine::OrderId       order_id{};
    engine::ParticipantId participant_id{};
    engine::InstrumentId  instrument_id{};
    engine::Side          side{};
    engine::OrderType     order_type{};
    engine::TimeInForce   time_in_force{};
    engine::Price         price{};
    engine::Quantity      quantity{};
    engine::Timestamp     timestamp{};
};

// ─── TradeStore ──────────────────────────────────────────────

class TradeStore {
public:
    void record_trade(const TradeRecord& rec);
    void record_order(const OrderRecord& rec);

    [[nodiscard]] std::vector<TradeRecord> query_by_participant(
        engine::ParticipantId pid, std::size_t limit = 0) const;

    [[nodiscard]] std::vector<TradeRecord> query_by_instrument(
        engine::InstrumentId iid, std::size_t limit = 0) const;

    [[nodiscard]] std::vector<TradeRecord> query_by_time_range(
        engine::Timestamp from_ts, engine::Timestamp to_ts,
        std::size_t limit = 0) const;

    [[nodiscard]] std::vector<OrderRecord> query_orders_by_participant(
        engine::ParticipantId pid, std::size_t limit = 0) const;

    /// Return all trades (for reconciliation).
    [[nodiscard]] std::vector<TradeRecord> query_all(std::size_t limit = 0) const;

    [[nodiscard]] std::size_t trade_count() const;
    [[nodiscard]] std::size_t order_count() const;

private:
    mutable std::mutex mtx_;
    std::vector<TradeRecord> trades_;
    std::vector<OrderRecord> orders_;
};

}  // namespace posttrade
