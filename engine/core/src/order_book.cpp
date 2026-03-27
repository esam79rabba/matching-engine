#include "engine/order_book.h"
#include <numeric>
#include <algorithm>

namespace engine {

// ═══════════════════════════════════════════════════════════════
// PriceLevel
// ═══════════════════════════════════════════════════════════════

void PriceLevel::append(Order* order) noexcept {
    order->prev_in_level = tail;
    order->next_in_level = nullptr;
    if (tail) {
        tail->next_in_level = order;
    } else {
        head = order;
    }
    tail = order;
    total_qty += order->visible_qty();
    ++order_count;
}

void PriceLevel::remove(Order* order) noexcept {
    if (order->prev_in_level) {
        order->prev_in_level->next_in_level = order->next_in_level;
    } else {
        head = order->next_in_level;
    }
    if (order->next_in_level) {
        order->next_in_level->prev_in_level = order->prev_in_level;
    } else {
        tail = order->prev_in_level;
    }
    total_qty -= order->visible_qty();
    --order_count;
    order->prev_in_level = nullptr;
    order->next_in_level = nullptr;
}

// ═══════════════════════════════════════════════════════════════
// OrderBook
// ═══════════════════════════════════════════════════════════════

OrderBook::OrderBook(InstrumentId instrument_id)
    : instrument_id_(instrument_id) {}

// ─── Insert ─────────────────────────────────────────────────

bool OrderBook::insert_limit_order(Order* order) {
    // Step 1: Try to match against opposite side
    Quantity remaining = try_match(order);

    if (remaining == 0) {
        // Fully filled — caller should release to pool
        return true;
    }

    // Step 2: Rest residual in book
    insert_into_side(order);
    orders_by_id_[order->order_id] = order;
    ++order_count_;

    if (listener_) {
        listener_->on_order_inserted(*order);
    }

    return false;  // Not fully filled; resting in book
}

// ─── Cancel ─────────────────────────────────────────────────

bool OrderBook::cancel_order(OrderId order_id) {
    auto it = orders_by_id_.find(order_id);
    if (it == orders_by_id_.end()) {
        return false;  // Not found
    }

    Order* order = it->second;
    remove_from_side(order);
    orders_by_id_.erase(it);
    --order_count_;

    if (listener_) {
        listener_->on_order_cancelled(*order, RejectCode::None);
    }

    return true;
}

// ─── Modify ─────────────────────────────────────────────────

bool OrderBook::modify_order(OrderId order_id, Price new_price, Quantity new_qty) {
    auto it = orders_by_id_.find(order_id);
    if (it == orders_by_id_.end()) {
        return false;
    }

    Order* order = it->second;

    // Price change: remove and re-insert (loses time priority)
    if (new_price != 0 && new_price != order->price) {
        remove_from_side(order);
        order->price = new_price;
        order->remaining_qty = (new_qty != 0) ? new_qty : order->remaining_qty;
        insert_into_side(order);
        if (listener_) {
            listener_->on_order_modified(*order);
        }
        return true;
    }

    // Quantity reduction only: in-place, retains priority
    if (new_qty != 0 && new_qty < order->remaining_qty) {
        // Find the price level to update aggregate
        Quantity delta = order->remaining_qty - new_qty;
        if (order->is_buy()) {
            auto lit = bids_.find(order->price);
            if (lit != bids_.end()) lit->second.total_qty -= delta;
        } else {
            auto lit = asks_.find(order->price);
            if (lit != asks_.end()) lit->second.total_qty -= delta;
        }
        order->remaining_qty = new_qty;
        if (listener_) {
            listener_->on_order_modified(*order);
        }
        return true;
    }

    return false;  // No change
}

// ─── Queries ────────────────────────────────────────────────

Price OrderBook::best_bid() const noexcept {
    if (bids_.empty()) return 0;
    return bids_.begin()->first;
}

Price OrderBook::best_ask() const noexcept {
    if (asks_.empty()) return 0;
    return asks_.begin()->first;
}

Quantity OrderBook::best_bid_qty() const noexcept {
    if (bids_.empty()) return 0;
    return bids_.begin()->second.total_qty;
}

Quantity OrderBook::best_ask_qty() const noexcept {
    if (asks_.empty()) return 0;
    return asks_.begin()->second.total_qty;
}

Order* OrderBook::find_order(OrderId order_id) const noexcept {
    auto it = orders_by_id_.find(order_id);
    return (it != orders_by_id_.end()) ? it->second : nullptr;
}

// ─── STP Check ──────────────────────────────────────────────

bool OrderBook::check_stp(const Order& aggressive, const Order& passive) const noexcept {
    if (aggressive.stp_group_id == 0 || passive.stp_group_id == 0) {
        return false;  // STP not active
    }
    return aggressive.stp_group_id == passive.stp_group_id;
    // FR-1: cancel-newest (aggressive) — caller handles the cancellation
}

// ═══════════════════════════════════════════════════════════════
// Internal helpers
// ═══════════════════════════════════════════════════════════════

// Internal: match aggressive order against a single side.
// Templated on map type to handle bids_ (greater<Price>) and asks_ (less<Price>).
/// STP result from checking self-trade prevention.
enum class STPAction : uint8_t {
    NoSTP,           // No self-trade — proceed normally
    CancelAggressive,// Cancel incoming order
    CancelPassive,   // Cancel resting order, continue matching
    CancelBoth,      // Cancel both sides
    Decrement,       // Reduce incoming qty by passive qty
};

template<typename MapT>
static Quantity do_match(MapT& opposite, Order* aggressive,
                         std::unordered_map<OrderId, Order*>& orders_by_id,
                         uint32_t& order_count,
                         OrderBookListener* listener,
                         STPMode stp_mode,
                         std::vector<OrderId>* stp_cancelled_ids) {
    while (aggressive->remaining_qty > 0 && !opposite.empty()) {
        auto level_it = opposite.begin();
        PriceLevel& level = level_it->second;

        if (aggressive->order_type == OrderType::Limit) {
            if (aggressive->is_buy() && aggressive->price < level.price) break;
            if (aggressive->is_sell() && aggressive->price > level.price) break;
        }

        Order* passive = level.head;
        while (passive != nullptr && aggressive->remaining_qty > 0) {
            Order* next = passive->next_in_level;

            // STP check — configurable mode
            if (aggressive->stp_group_id != 0 &&
                passive->stp_group_id != 0 &&
                aggressive->stp_group_id == passive->stp_group_id) {

                switch (stp_mode) {
                    case STPMode::CancelNewest:
                        // Cancel the aggressive (incoming) order
                        aggressive->remaining_qty = 0;
                        if (listener) {
                            listener->on_order_cancelled(*aggressive, RejectCode::STPCancelled);
                        }
                        return 0;

                    case STPMode::CancelOldest:
                        // Cancel the passive (resting) order, continue matching
                        if (stp_cancelled_ids) stp_cancelled_ids->push_back(passive->order_id);
                        level.remove(passive);
                        orders_by_id.erase(passive->order_id);
                        --order_count;
                        if (listener) {
                            listener->on_order_cancelled(*passive, RejectCode::STPCancelled);
                        }
                        passive = next;
                        continue;  // Try next passive order

                    case STPMode::CancelBoth:
                        // Cancel both sides
                        if (stp_cancelled_ids) stp_cancelled_ids->push_back(passive->order_id);
                        level.remove(passive);
                        orders_by_id.erase(passive->order_id);
                        --order_count;
                        if (listener) {
                            listener->on_order_cancelled(*passive, RejectCode::STPCancelled);
                            listener->on_order_cancelled(*aggressive, RejectCode::STPCancelled);
                        }
                        aggressive->remaining_qty = 0;
                        if (level.is_empty()) opposite.erase(level_it);
                        return 0;

                    case STPMode::Decrement: {
                        // Reduce incoming qty by passive qty
                        Quantity passive_qty = passive->remaining_qty;
                        if (aggressive->remaining_qty <= passive_qty) {
                            // Incoming fully absorbed — cancel incoming
                            aggressive->remaining_qty = 0;
                            if (listener) {
                                listener->on_order_cancelled(*aggressive, RejectCode::STPCancelled);
                            }
                            return 0;
                        }
                        // Reduce incoming, cancel passive
                        aggressive->remaining_qty -= passive_qty;
                        aggressive->original_qty -= passive_qty;
                        if (stp_cancelled_ids) stp_cancelled_ids->push_back(passive->order_id);
                        level.remove(passive);
                        orders_by_id.erase(passive->order_id);
                        --order_count;
                        if (listener) {
                            listener->on_order_cancelled(*passive, RejectCode::STPCancelled);
                        }
                        passive = next;
                        continue;  // Continue matching with reduced qty
                    }
                }
            }

            // Match against visible quantity (iceberg shows only display_qty)
            Quantity matchable = passive->visible_qty();
            Quantity fill_qty = std::min(aggressive->remaining_qty, matchable);
            Price fill_price = passive->price;

            aggressive->remaining_qty -= fill_qty;
            aggressive->cumulative_filled += fill_qty;
            passive->remaining_qty -= fill_qty;
            passive->cumulative_filled += fill_qty;
            // Decrease display_qty if iceberg
            if (passive->display_qty > 0) {
                passive->display_qty = (passive->display_qty > fill_qty)
                    ? passive->display_qty - fill_qty : 0;
            }
            level.total_qty -= fill_qty;

            if (listener) {
                OrderId buy_id  = aggressive->is_buy() ? aggressive->order_id : passive->order_id;
                OrderId sell_id = aggressive->is_sell() ? aggressive->order_id : passive->order_id;
                listener->on_trade(buy_id, sell_id, fill_price, fill_qty, aggressive->side);
            }

            if (passive->remaining_qty == 0) {
                // Fully consumed — remove from book
                level.remove(passive);
                orders_by_id.erase(passive->order_id);
                --order_count;
            } else if (passive->display_qty == 0 && passive->remaining_qty > 0
                       && (passive->flags & FLAG_ICEBERG) != 0) {
                // Iceberg: visible portion exhausted but hidden qty remains.
                // Replenish display_qty and move to back of queue (loses time priority).
                Quantity replenish = std::min(passive->remaining_qty, passive->original_qty);
                passive->display_qty = replenish;
                level.total_qty += replenish;

                // Move to tail of level (lose time priority) if not already last
                if (passive->next_in_level != nullptr) {
                    // Unlink from current position
                    if (passive->prev_in_level)
                        passive->prev_in_level->next_in_level = passive->next_in_level;
                    else
                        level.head = passive->next_in_level;
                    passive->next_in_level->prev_in_level = passive->prev_in_level;

                    // Append to tail
                    passive->prev_in_level = level.tail;
                    passive->next_in_level = nullptr;
                    if (level.tail) level.tail->next_in_level = passive;
                    level.tail = passive;
                }
            }

            passive = next;
        }

        if (level.is_empty()) {
            opposite.erase(level_it);
        }
    }
    return aggressive->remaining_qty;
}

/// Pro-rata matching: allocate fills proportionally to order size within a price level.
/// For each price level that crosses:
///   1. Sum total qty at the level
///   2. Each order gets: floor(order_qty / total_qty * fill_available)
///   3. Rounding remainder goes to largest order (time priority for ties)
template<typename MapT>
static Quantity do_match_pro_rata(MapT& opposite, Order* aggressive,
                                  std::unordered_map<OrderId, Order*>& orders_by_id,
                                  uint32_t& order_count,
                                  OrderBookListener* listener,
                                  STPMode stp_mode,
                                  std::vector<OrderId>* stp_cancelled_ids,
                                  Quantity min_fill) {
    while (aggressive->remaining_qty > 0 && !opposite.empty()) {
        auto level_it = opposite.begin();
        PriceLevel& level = level_it->second;

        if (aggressive->order_type == OrderType::Limit) {
            if (aggressive->is_buy() && aggressive->price < level.price) break;
            if (aggressive->is_sell() && aggressive->price > level.price) break;
        }

        // Collect all orders at this level (skip STP conflicts)
        struct LevelOrder { Order* order; Quantity alloc; };
        std::vector<LevelOrder> eligible;
        Quantity level_total = 0;

        Order* passive = level.head;
        while (passive != nullptr) {
            // STP check
            if (aggressive->stp_group_id != 0 &&
                passive->stp_group_id != 0 &&
                aggressive->stp_group_id == passive->stp_group_id) {
                // Handle STP same as FIFO mode for the conflicting order
                Order* next = passive->next_in_level;
                if (stp_mode == STPMode::CancelNewest) {
                    aggressive->remaining_qty = 0;
                    if (listener) listener->on_order_cancelled(*aggressive, RejectCode::STPCancelled);
                    return 0;
                } else if (stp_mode == STPMode::CancelBoth) {
                    if (stp_cancelled_ids) stp_cancelled_ids->push_back(passive->order_id);
                    level.remove(passive);
                    orders_by_id.erase(passive->order_id);
                    --order_count;
                    if (listener) {
                        listener->on_order_cancelled(*passive, RejectCode::STPCancelled);
                        listener->on_order_cancelled(*aggressive, RejectCode::STPCancelled);
                    }
                    aggressive->remaining_qty = 0;
                    if (level.is_empty()) opposite.erase(level_it);
                    return 0;
                } else {
                    // CancelOldest or Decrement: skip this order
                    if (stp_cancelled_ids) stp_cancelled_ids->push_back(passive->order_id);
                    level.remove(passive);
                    orders_by_id.erase(passive->order_id);
                    --order_count;
                    if (listener) listener->on_order_cancelled(*passive, RejectCode::STPCancelled);
                    if (stp_mode == STPMode::Decrement && aggressive->remaining_qty <= passive->remaining_qty) {
                        aggressive->remaining_qty = 0;
                        if (listener) listener->on_order_cancelled(*aggressive, RejectCode::STPCancelled);
                        if (level.is_empty()) opposite.erase(level_it);
                        return 0;
                    }
                    passive = next;
                    continue;
                }
            }
            eligible.push_back({passive, 0});
            level_total += passive->visible_qty();
            passive = passive->next_in_level;
        }

        if (eligible.empty()) {
            if (level.is_empty()) opposite.erase(level_it);
            continue;
        }

        // How much can we fill at this level?
        Quantity fill_available = std::min(aggressive->remaining_qty, level_total);

        // Pro-rata allocation
        Quantity allocated = 0;
        for (auto& e : eligible) {
            // Proportional: floor(order_qty * fill_available / level_total)
            e.alloc = static_cast<Quantity>(
                static_cast<__uint128_t>(e.order->visible_qty()) * fill_available / level_total);
            // Enforce minimum fill
            if (e.alloc > 0 && e.alloc < min_fill) e.alloc = 0;
            allocated += e.alloc;
        }

        // Distribute remainder to orders by size (descending), time priority for ties
        Quantity remainder = fill_available - allocated;
        if (remainder > 0) {
            // Sort eligible by remaining_qty descending (stable to preserve time priority)
            std::vector<size_t> idx(eligible.size());
            std::iota(idx.begin(), idx.end(), 0);
            std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
                return eligible[a].order->remaining_qty > eligible[b].order->remaining_qty;
            });
            for (size_t i : idx) {
                if (remainder == 0) break;
                Quantity extra = std::min(remainder,
                    eligible[i].order->remaining_qty - eligible[i].alloc);
                eligible[i].alloc += extra;
                remainder -= extra;
            }
        }

        // Execute fills
        for (auto& e : eligible) {
            if (e.alloc == 0) continue;

            Quantity fill_qty = e.alloc;
            Price fill_price = e.order->price;

            aggressive->remaining_qty -= fill_qty;
            aggressive->cumulative_filled += fill_qty;
            e.order->remaining_qty -= fill_qty;
            e.order->cumulative_filled += fill_qty;
            level.total_qty -= fill_qty;

            if (listener) {
                OrderId buy_id  = aggressive->is_buy() ? aggressive->order_id : e.order->order_id;
                OrderId sell_id = aggressive->is_sell() ? aggressive->order_id : e.order->order_id;
                listener->on_trade(buy_id, sell_id, fill_price, fill_qty, aggressive->side);
            }

            if (e.order->remaining_qty == 0) {
                level.remove(e.order);
                orders_by_id.erase(e.order->order_id);
                --order_count;
            }
        }

        if (level.is_empty()) {
            opposite.erase(level_it);
        }
    }
    return aggressive->remaining_qty;
}

Quantity OrderBook::try_match(Order* aggressive) {
    stp_cancelled_ids_.clear();
    if (matching_algo_ == MatchingAlgorithm::ProRata) {
        if (aggressive->is_buy()) {
            return do_match_pro_rata(asks_, aggressive, orders_by_id_, order_count_, listener_,
                                     stp_mode_, &stp_cancelled_ids_, min_pro_rata_fill_);
        } else {
            return do_match_pro_rata(bids_, aggressive, orders_by_id_, order_count_, listener_,
                                     stp_mode_, &stp_cancelled_ids_, min_pro_rata_fill_);
        }
    }
    if (aggressive->is_buy()) {
        return do_match(asks_, aggressive, orders_by_id_, order_count_, listener_,
                        stp_mode_, &stp_cancelled_ids_);
    } else {
        return do_match(bids_, aggressive, orders_by_id_, order_count_, listener_,
                        stp_mode_, &stp_cancelled_ids_);
    }
}

void OrderBook::insert_into_side(Order* order) {
    PriceLevel& level = get_or_create_level(order->side, order->price);
    level.append(order);
}

void OrderBook::insert_without_matching(Order* order) {
    order->prev_in_level = nullptr;
    order->next_in_level = nullptr;
    insert_into_side(order);
    orders_by_id_[order->order_id] = order;
    ++order_count_;
    if (listener_) listener_->on_order_inserted(*order);
}

void OrderBook::remove_from_side(Order* order) {
    if (order->is_buy()) {
        auto it = bids_.find(order->price);
        if (it != bids_.end()) {
            it->second.remove(order);
            if (it->second.is_empty()) bids_.erase(it);
        }
    } else {
        auto it = asks_.find(order->price);
        if (it != asks_.end()) {
            it->second.remove(order);
            if (it->second.is_empty()) asks_.erase(it);
        }
    }
}

PriceLevel& OrderBook::get_or_create_level(Side side, Price price) {
    if (side == Side::Buy) {
        auto [it, inserted] = bids_.try_emplace(price, price);
        return it->second;
    } else {
        auto [it, inserted] = asks_.try_emplace(price, price);
        return it->second;
    }
}

}  // namespace engine
