#include "posttrade/trade_store.h"

#include <algorithm>
#include <mutex>

namespace posttrade {

void TradeStore::record_trade(const TradeRecord& rec) {
    std::lock_guard<std::mutex> lock(mtx_);
    trades_.push_back(rec);
}

void TradeStore::record_order(const OrderRecord& rec) {
    std::lock_guard<std::mutex> lock(mtx_);
    orders_.push_back(rec);
}

std::vector<TradeRecord> TradeStore::query_by_participant(
    engine::ParticipantId pid, std::size_t limit) const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<TradeRecord> result;
    for (auto it = trades_.rbegin(); it != trades_.rend(); ++it) {
        if (it->buyer_id == pid || it->seller_id == pid) {
            result.push_back(*it);
            if (limit > 0 && result.size() >= limit) break;
        }
    }
    return result;
}

std::vector<TradeRecord> TradeStore::query_by_instrument(
    engine::InstrumentId iid, std::size_t limit) const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<TradeRecord> result;
    for (auto it = trades_.rbegin(); it != trades_.rend(); ++it) {
        if (it->instrument_id == iid) {
            result.push_back(*it);
            if (limit > 0 && result.size() >= limit) break;
        }
    }
    return result;
}

std::vector<TradeRecord> TradeStore::query_by_time_range(
    engine::Timestamp from_ts, engine::Timestamp to_ts,
    std::size_t limit) const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<TradeRecord> result;
    for (auto it = trades_.rbegin(); it != trades_.rend(); ++it) {
        if (it->timestamp >= from_ts && it->timestamp <= to_ts) {
            result.push_back(*it);
            if (limit > 0 && result.size() >= limit) break;
        }
    }
    return result;
}

std::vector<OrderRecord> TradeStore::query_orders_by_participant(
    engine::ParticipantId pid, std::size_t limit) const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<OrderRecord> result;
    for (auto it = orders_.rbegin(); it != orders_.rend(); ++it) {
        if (it->participant_id == pid) {
            result.push_back(*it);
            if (limit > 0 && result.size() >= limit) break;
        }
    }
    return result;
}

std::vector<TradeRecord> TradeStore::query_all(std::size_t limit) const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (limit == 0 || limit >= trades_.size()) {
        return trades_;
    }
    // Return most recent trades
    return std::vector<TradeRecord>(trades_.end() - static_cast<ptrdiff_t>(limit), trades_.end());
}

std::size_t TradeStore::trade_count() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return trades_.size();
}

std::size_t TradeStore::order_count() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return orders_.size();
}

}  // namespace posttrade
