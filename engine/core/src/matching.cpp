#include "engine/matching.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace engine {

// ═══════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════

MatchingEngine::MatchingEngine(const InstrumentConfig& config, OrderPool& pool)
    : config_(config)
    , book_(config.instrument_id)
    , pool_(pool) {
    // Default: instrument starts in Continuous for backward compatibility.
    state_machine_.force_state(InstrumentState::Continuous);
    // Install fill capture listener on the order book
    book_.set_listener(&fill_capture_);
}

// ═══════════════════════════════════════════════════════════════
// New Order Processing
// ═══════════════════════════════════════════════════════════════

bool MatchingEngine::transition_state(InstrumentState new_state) {
    return state_machine_.transition(new_state);
}

MatchResult MatchingEngine::process_new_order(Order* order) {
    MatchResult result;

    // ── Step 0: State check ─────────────────────────────────
    if (!state_machine_.accepts_new_orders()) {
        result.action = MatchResult::Action::Rejected;
        result.reject_code = RejectCode::InstrumentHalted;
        pool_.release(order);
        return result;
    }

    // ── Step 1: Validate ────────────────────────────────────
    RejectCode rc = validate_new_order(*order);
    if (rc != RejectCode::None) {
        result.action = MatchResult::Action::Rejected;
        result.reject_code = rc;
        pool_.release(order);
        return result;
    }

    // ── Step 2: FOK pre-check ───────────────────────────────
    if (order->time_in_force == TimeInForce::FOK) {
        if (!can_fill_completely(order->side, order->price, order->remaining_qty)) {
            pool_.release(order);
            result.action = MatchResult::Action::Rejected;
            result.reject_code = RejectCode::FOKInsufficientQty;
            return result;
        }
    }

    // ── Step 2b: PreOpen — insert without matching ──────────
    if (state_machine_.state() == InstrumentState::PreOpen) {
        // During PreOpen, orders are collected into the book without matching.
        // They will be matched during the opening auction.
        if (order->order_type == OrderType::Market) {
            // Market orders are not valid during PreOpen (no reference price)
            result.action = MatchResult::Action::Rejected;
            result.reject_code = RejectCode::InvalidOrderType;
            pool_.release(order);
            return result;
        }
        // Insert into book without matching (auction collection)
        book_.insert_without_matching(order);
        result.action = MatchResult::Action::Inserted;
        result.order_resting = true;
        return result;
    }

    // ── Step 3: Match against opposite side ─────────────────
    // Install fill capture for this order processing call
    fill_capture_.result = &result;
    fill_capture_.trade_counter = &next_trade_id_;

    bool fully_consumed;

    if (order->order_type == OrderType::Market) {
        // Market orders: set a synthetic crossing price to sweep the book.
        // The actual fill price is always the passive order's price.
        order->price = order->is_buy() ? INT64_MAX : 1;
        fully_consumed = book_.insert_limit_order(order);

        // Handle residual per frozen policy
        if (!fully_consumed && order->remaining_qty > 0) {
            handle_market_residual(order, result);
            return result;
        }
    } else {
        // Limit orders: normal matching + insertion
        fully_consumed = book_.insert_limit_order(order);
    }

    // ── Step 4: Handle IOC residual ─────────────────────────
    if (!fully_consumed && order->time_in_force == TimeInForce::IOC) {
        book_.cancel_order(order->order_id);
        result.action = MatchResult::Action::Cancelled;
        result.reject_code = RejectCode::IOCResidualCancel;
        result.cancelled_ids.push_back(order->order_id);
        pool_.release(order);
        return result;
    }

    // ── Step 5: Determine result ────────────────────────────
    fill_capture_.result = nullptr;
    fill_capture_.trade_counter = nullptr;

    if (fully_consumed) {
        result.action = MatchResult::Action::Filled;
    } else {
        result.action = MatchResult::Action::Inserted;
        result.order_resting = true;
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════
// Cancel
// ═══════════════════════════════════════════════════════════════

MatchResult MatchingEngine::process_cancel(OrderId order_id) {
    MatchResult result;

    if (!state_machine_.accepts_cancels()) {
        result.action = MatchResult::Action::Rejected;
        result.reject_code = RejectCode::InstrumentHalted;
        return result;
    }

    Order* order = book_.find_order(order_id);
    if (order == nullptr) {
        result.action = MatchResult::Action::Rejected;
        result.reject_code = RejectCode::None;
        return result;
    }

    book_.cancel_order(order_id);
    result.action = MatchResult::Action::Cancelled;
    result.cancelled_ids.push_back(order_id);
    pool_.release(order);

    return result;
}

// ═══════════════════════════════════════════════════════════════
// Modify
// ═══════════════════════════════════════════════════════════════

MatchResult MatchingEngine::process_modify(OrderId order_id, Price new_price, Quantity new_qty) {
    MatchResult result;

    if (!state_machine_.accepts_modifies()) {
        result.action = MatchResult::Action::Rejected;
        result.reject_code = RejectCode::InstrumentHalted;
        return result;
    }

    Order* order = book_.find_order(order_id);
    if (order == nullptr) {
        result.action = MatchResult::Action::Rejected;
        return result;
    }

    // Validate new price if changing
    if (new_price != 0 && new_price != order->price) {
        if (!is_tick_aligned(new_price)) {
            result.action = MatchResult::Action::Rejected;
            result.reject_code = RejectCode::InvalidTick;
            return result;
        }
        if (!is_within_static_band(new_price)) {
            result.action = MatchResult::Action::Rejected;
            result.reject_code = RejectCode::PriceOutsideBands;
            return result;
        }
    }

    // Validate new quantity if changing
    if (new_qty != 0 && !is_lot_aligned(new_qty)) {
        result.action = MatchResult::Action::Rejected;
        result.reject_code = RejectCode::InvalidLot;
        return result;
    }

    bool modified = book_.modify_order(order_id, new_price, new_qty);
    if (modified) {
        result.action = MatchResult::Action::Modified;
    } else {
        result.action = MatchResult::Action::Rejected;
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════
// Opening Auction
// ═══════════════════════════════════════════════════════════════

std::pair<Price, Quantity> MatchingEngine::calculate_auction_price() const {
    // Collect all distinct price levels from bids and asks
    // For each candidate price P:
    //   - Cumulative bid qty = sum of qty at prices >= P
    //   - Cumulative ask qty = sum of qty at prices <= P
    //   - Executable volume = min(cum_bid, cum_ask)
    // Choose P that maximizes executable volume.

    // Gather all unique prices from the book
    std::vector<Price> candidate_prices;
    book_.for_each_order([&](const Order& o) {
        candidate_prices.push_back(o.price);
    });

    if (candidate_prices.empty()) return {0, 0};

    // Sort and deduplicate
    std::sort(candidate_prices.begin(), candidate_prices.end());
    candidate_prices.erase(
        std::unique(candidate_prices.begin(), candidate_prices.end()),
        candidate_prices.end());

    Price best_price = 0;
    Quantity best_volume = 0;

    for (Price p : candidate_prices) {
        Quantity cum_bid = 0;
        Quantity cum_ask = 0;

        book_.for_each_order([&](const Order& o) {
            if (o.side == Side::Buy && o.price >= p) {
                cum_bid += o.remaining_qty;
            } else if (o.side == Side::Sell && o.price <= p) {
                cum_ask += o.remaining_qty;
            }
        });

        Quantity executable = (cum_bid < cum_ask) ? cum_bid : cum_ask;
        if (executable > best_volume) {
            best_volume = executable;
            best_price = p;
        }
    }

    return {best_price, best_volume};
}

MatchResult MatchingEngine::run_opening_auction() {
    MatchResult result;

    // Calculate the auction price
    auto [auction_price, auction_volume] = calculate_auction_price();

    if (auction_price == 0 || auction_volume == 0) {
        // No crossing interest — transition to Continuous with no trades
        (void)state_machine_.transition(InstrumentState::Continuous);
        result.action = MatchResult::Action::Inserted;  // No trades, book unchanged
        return result;
    }

    // Install fill capture
    fill_capture_.result = &result;
    fill_capture_.trade_counter = &next_trade_id_;

    // Execute auction: match all orders that cross at the auction price.
    // Strategy: collect matchable orders, remove them from the book,
    // execute fills, then re-insert any with remaining quantity.
    Quantity remaining_volume = auction_volume;

    // Collect matchable bids (price >= auction_price) and asks (price <= auction_price)
    std::vector<Order*> matchable_bids;
    std::vector<Order*> matchable_asks;

    book_.for_each_order([&](const Order& o) {
        if (o.side == Side::Buy && o.price >= auction_price) {
            matchable_bids.push_back(const_cast<Order*>(&o));
        } else if (o.side == Side::Sell && o.price <= auction_price) {
            matchable_asks.push_back(const_cast<Order*>(&o));
        }
    });

    // Remove all matchable orders from the book first
    for (Order* o : matchable_bids) book_.cancel_order(o->order_id);
    for (Order* o : matchable_asks) book_.cancel_order(o->order_id);

    size_t bid_idx = 0;
    size_t ask_idx = 0;

    while (remaining_volume > 0 && bid_idx < matchable_bids.size() && ask_idx < matchable_asks.size()) {
        Order* bid = matchable_bids[bid_idx];
        Order* ask = matchable_asks[ask_idx];

        Quantity fill_qty = remaining_volume;
        if (bid->remaining_qty < fill_qty) fill_qty = bid->remaining_qty;
        if (ask->remaining_qty < fill_qty) fill_qty = ask->remaining_qty;

        // Generate trade
        MatchResult::Fill fill;
        fill.trade_id = next_trade_id_++;
        fill.buy_order_id = bid->order_id;
        fill.sell_order_id = ask->order_id;
        fill.fill_price = auction_price;
        fill.fill_qty = fill_qty;
        fill.aggressor_side = Side::Buy;  // Convention: buyer is aggressor in auction
        result.fills.push_back(fill);

        bid->remaining_qty -= fill_qty;
        bid->cumulative_filled += fill_qty;
        ask->remaining_qty -= fill_qty;
        ask->cumulative_filled += fill_qty;
        remaining_volume -= fill_qty;

        if (bid->remaining_qty == 0) {
            pool_.release(bid);
            ++bid_idx;
        }
        if (ask->remaining_qty == 0) {
            pool_.release(ask);
            ++ask_idx;
        }
    }

    // Re-insert any partially-filled orders back into the book
    for (size_t i = bid_idx; i < matchable_bids.size(); ++i) {
        if (matchable_bids[i]->remaining_qty > 0) {
            book_.insert_without_matching(matchable_bids[i]);
        } else {
            pool_.release(matchable_bids[i]);
        }
    }
    for (size_t i = ask_idx; i < matchable_asks.size(); ++i) {
        if (matchable_asks[i]->remaining_qty > 0) {
            book_.insert_without_matching(matchable_asks[i]);
        } else {
            pool_.release(matchable_asks[i]);
        }
    }

    fill_capture_.result = nullptr;
    fill_capture_.trade_counter = nullptr;

    // Transition to Continuous trading
    (void)state_machine_.transition(InstrumentState::Continuous);

    result.action = result.fills.empty() ? MatchResult::Action::Inserted : MatchResult::Action::Filled;
    return result;
}

// ═══════════════════════════════════════════════════════════════
// Validation — generic, parameterized by InstrumentConfig
// ═══════════════════════════════════════════════════════════════

RejectCode MatchingEngine::validate_new_order(const Order& order) const noexcept {
    // Quantity checks (generic — lot size from config, not hardcoded)
    if (order.remaining_qty == 0) {
        return RejectCode::InvalidLot;
    }
    if (order.remaining_qty < config_.min_quantity) {
        return RejectCode::InvalidLot;
    }
    if (order.remaining_qty > config_.max_quantity) {
        return RejectCode::QuantityExceedsMax;
    }
    if (!is_lot_aligned(order.remaining_qty)) {
        return RejectCode::InvalidLot;
    }

    // Price checks (limit orders only; generic — tick from config)
    if (order.order_type == OrderType::Limit) {
        if (order.price <= 0) {
            return RejectCode::PriceOutsideBands;
        }
        if (!is_tick_aligned(order.price)) {
            return RejectCode::InvalidTick;
        }
        if (!is_within_static_band(order.price)) {
            return RejectCode::PriceOutsideBands;
        }
    }

    return RejectCode::None;
}

bool MatchingEngine::is_tick_aligned(Price price) const noexcept {
    if (config_.tick_size <= 0) return true;  // No tick constraint
    return (price % config_.tick_size) == 0;
}

bool MatchingEngine::is_lot_aligned(Quantity qty) const noexcept {
    if (config_.lot_size <= 0) return true;  // No lot constraint
    return (qty % config_.lot_size) == 0;
}

bool MatchingEngine::is_within_static_band(Price price) const noexcept {
    if (config_.static_ref_price <= 0 || config_.static_band_pct <= 0.0) {
        return true;  // No band configured
    }
    double deviation = std::abs(static_cast<double>(price - config_.static_ref_price))
                       / static_cast<double>(config_.static_ref_price) * 100.0;
    return deviation <= config_.static_band_pct;
}

// ═══════════════════════════════════════════════════════════════
// Market-Order Residual — Frozen Policy (ADR-004)
// ═══════════════════════════════════════════════════════════════
//
// This logic is generic. The residual_policy and collar_ticks values
// come from InstrumentConfig (set by reference data / venue rules).
// No asset-class-specific logic here.

void MatchingEngine::handle_market_residual(Order* order, MatchResult& result) {
    switch (config_.residual_policy) {

        case ResidualPolicy::Cancel:
            // Default: cancel unfilled residual
            book_.cancel_order(order->order_id);
            result.action = MatchResult::Action::Cancelled;
            result.reject_code = RejectCode::MarketResidualCancel;
            result.cancelled_ids.push_back(order->order_id);
            pool_.release(order);
            break;

        case ResidualPolicy::MarketToLimitCollar: {
            if (order->cumulative_filled == 0) {
                // Never filled — no reference price for collar; cancel
                book_.cancel_order(order->order_id);
                result.action = MatchResult::Action::Cancelled;
                result.reject_code = RejectCode::MarketResidualCancel;
                result.cancelled_ids.push_back(order->order_id);
                pool_.release(order);
                break;
            }

            // Compute collar price from last fill (approximated as book's last trade)
            // In production, the last fill price would be tracked explicitly.
            Price last_fill = book_.best_ask();  // Approximation for prototype
            if (last_fill <= 0) last_fill = book_.best_bid();
            if (last_fill <= 0) {
                // No reference — cancel
                book_.cancel_order(order->order_id);
                result.action = MatchResult::Action::Cancelled;
                result.reject_code = RejectCode::MarketResidualCancel;
                pool_.release(order);
                break;
            }

            Price collar_offset = static_cast<Price>(config_.collar_ticks) * config_.tick_size;
            Price collar_price = order->is_buy()
                ? last_fill + collar_offset
                : last_fill - collar_offset;

            if (collar_price <= 0 || !is_within_static_band(collar_price)) {
                // Collar would breach bands — cancel
                result.action = MatchResult::Action::Cancelled;
                result.reject_code = RejectCode::CollarOutsideBands;
                result.cancelled_ids.push_back(order->order_id);
                pool_.release(order);
                break;
            }

            // Convert to limit at collar price and insert
            order->order_type = OrderType::Limit;
            order->price = collar_price;
            book_.insert_limit_order(order);
            result.action = MatchResult::Action::Inserted;
            result.order_resting = true;
            break;
        }
    }
    // NOTE: Unprotected resting at last fill price is NOT implemented (ADR-004).
}

// ═══════════════════════════════════════════════════════════════
// FOK Pre-Check — read-only book scan
// ═══════════════════════════════════════════════════════════════

bool MatchingEngine::can_fill_completely(Side side, Price price, Quantity qty) const {
    Quantity accumulated = 0;

    if (side == Side::Buy) {
        // Buy order matches against asks. Walk asks from best (lowest) to worst.
        // Accumulate quantity at prices <= the buy limit price.
        book_.for_each_order([&](const Order& o) {
            if (o.side == Side::Sell && o.price <= price) {
                accumulated += o.remaining_qty;
            }
        });
    } else {
        // Sell order matches against bids. Walk bids from best (highest) to worst.
        // Accumulate quantity at prices >= the sell limit price.
        book_.for_each_order([&](const Order& o) {
            if (o.side == Side::Buy && o.price >= price) {
                accumulated += o.remaining_qty;
            }
        });
    }

    return accumulated >= qty;
}

}  // namespace engine
