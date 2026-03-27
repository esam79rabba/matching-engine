#include "engine/order_book.h"

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
    total_qty += order->remaining_qty;
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
    total_qty -= order->remaining_qty;
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
template<typename MapT>
static Quantity do_match(MapT& opposite, Order* aggressive,
                         std::unordered_map<OrderId, Order*>& orders_by_id,
                         uint32_t& order_count,
                         OrderBookListener* listener) {
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

            // STP check (inline — same STP group → cancel aggressive)
            if (aggressive->stp_group_id != 0 &&
                passive->stp_group_id != 0 &&
                aggressive->stp_group_id == passive->stp_group_id) {
                aggressive->remaining_qty = 0;
                if (listener) {
                    listener->on_order_cancelled(*aggressive, RejectCode::STPCancelled);
                }
                return 0;
            }

            Quantity fill_qty = std::min(aggressive->remaining_qty, passive->remaining_qty);
            Price fill_price = passive->price;

            aggressive->remaining_qty -= fill_qty;
            aggressive->cumulative_filled += fill_qty;
            passive->remaining_qty -= fill_qty;
            passive->cumulative_filled += fill_qty;
            level.total_qty -= fill_qty;

            if (listener) {
                OrderId buy_id  = aggressive->is_buy() ? aggressive->order_id : passive->order_id;
                OrderId sell_id = aggressive->is_sell() ? aggressive->order_id : passive->order_id;
                listener->on_trade(buy_id, sell_id, fill_price, fill_qty, aggressive->side);
            }

            if (passive->remaining_qty == 0) {
                level.remove(passive);
                orders_by_id.erase(passive->order_id);
                --order_count;
            }

            passive = next;
        }

        if (level.is_empty()) {
            opposite.erase(level_it);
        }
    }
    return aggressive->remaining_qty;
}

Quantity OrderBook::try_match(Order* aggressive) {
    if (aggressive->is_buy()) {
        return do_match(asks_, aggressive, orders_by_id_, order_count_, listener_);
    } else {
        return do_match(bids_, aggressive, orders_by_id_, order_count_, listener_);
    }
}

void OrderBook::insert_into_side(Order* order) {
    PriceLevel& level = get_or_create_level(order->side, order->price);
    level.append(order);
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
