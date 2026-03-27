#pragma once

#include "engine/order_book.h"
#include "engine/types.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace marketdata {

struct DepthLevel {
    engine::Price price;
    engine::Quantity quantity;
    uint32_t order_count;
};

struct DepthSnapshot {
    engine::InstrumentId instrument_id;
    std::vector<DepthLevel> bids;   // Best first (highest price)
    std::vector<DepthLevel> asks;   // Best first (lowest price)
    uint64_t sequence;
    engine::Timestamp timestamp;
};

/// Builds full-depth (L2) snapshots from the order book and tracks
/// incremental depth updates for efficient publication.
class DepthPublisher {
public:
    /// Build a full depth snapshot from an order book, up to max_levels per side.
    DepthSnapshot build_snapshot(const engine::OrderBook& book, uint32_t max_levels = 20) const;

    /// Record an incremental depth change for later retrieval.
    void on_depth_change(engine::InstrumentId instrument_id, engine::Side side,
                         engine::Price price, engine::Quantity new_qty, uint32_t order_count);

    /// Retrieve and clear pending incremental updates for an instrument.
    std::vector<DepthLevel> get_pending_updates(engine::InstrumentId instrument_id);

    /// Build a recovery snapshot for a newly-connected subscriber.
    /// Equivalent to build_snapshot but stamps with the current sequence number.
    DepthSnapshot get_recovery_snapshot(engine::InstrumentId instrument_id,
                                        const engine::OrderBook& book) const;

    /// Advance and return the next sequence number.
    uint64_t next_sequence() { return ++sequence_; }

    /// Current sequence number (for testing).
    [[nodiscard]] uint64_t sequence() const noexcept { return sequence_; }

private:
    uint64_t sequence_ = 0;
    std::unordered_map<engine::InstrumentId, std::vector<DepthLevel>> pending_updates_;
};

}  // namespace marketdata
