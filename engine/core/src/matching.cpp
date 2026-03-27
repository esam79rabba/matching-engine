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
    fill_capture_.last_traded_price = &last_traded_price_;
    book_.set_listener(&fill_capture_);
    // Set STP mode and matching algorithm from instrument config
    book_.set_stp_mode(config.stp_mode);
    book_.set_matching_algo(config.matching_algo, config.min_pro_rata_fill);
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

    // ── Step 1b: AuctionOnly TIF — reject outside auction states
    if (order->time_in_force == TimeInForce::AuctionOnly) {
        auto st = state_machine_.state();
        if (st != InstrumentState::PreOpen &&
            st != InstrumentState::Auction &&
            st != InstrumentState::ClosingAuction) {
            result.action = MatchResult::Action::Rejected;
            result.reject_code = RejectCode::InvalidOrderType;
            pool_.release(order);
            return result;
        }
    }

    // ── Step 1c: Calculate initial peg price ────────────────
    if (order->peg_type != PegType::None) {
        Price peg_price = calculate_peg_price(*order);
        if (peg_price <= 0) {
            result.action = MatchResult::Action::Rejected;
            result.reject_code = RejectCode::PegNoReference;
            pool_.release(order);
            return result;
        }
        order->price = peg_price;
    }

    // ── Step 1d: Handle stop orders ─────────────────────────
    if (order->order_type == OrderType::Stop || order->order_type == OrderType::StopLimit) {
        stop_queue_.add(order);
        result.action = MatchResult::Action::Inserted;
        result.order_resting = true;
        return result;
    }

    // ── Step 2b: PreOpen / Auction — insert without matching ─
    if (state_machine_.state() == InstrumentState::PreOpen ||
        state_machine_.state() == InstrumentState::Auction) {
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

    // ── Step 2c: ClosingAuction — insert without matching ───
    if (state_machine_.state() == InstrumentState::ClosingAuction) {
        if (order->order_type == OrderType::Market) {
            result.action = MatchResult::Action::Rejected;
            result.reject_code = RejectCode::InvalidOrderType;
            pool_.release(order);
            return result;
        }
        book_.insert_without_matching(order);
        result.action = MatchResult::Action::Inserted;
        result.order_resting = true;
        return result;
    }

    // ── Step 2d: Post-Only check ────────────────────────────
    if (order->is_post_only() && order->order_type == OrderType::Limit) {
        // Post-only orders must NOT match — they should only rest in the book.
        // If the limit price would cross the opposite side, reject.
        Price best_opposite = order->is_buy() ? book_.best_ask() : book_.best_bid();
        if (best_opposite > 0) {
            bool would_cross = order->is_buy()
                ? (order->price >= best_opposite)
                : (order->price <= best_opposite);
            if (would_cross) {
                result.action = MatchResult::Action::Rejected;
                result.reject_code = RejectCode::PostOnlyWouldMatch;
                pool_.release(order);
                return result;
            }
        }
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

    // ── Step 5: Collect STP-cancelled order IDs ─────────────
    for (OrderId stp_id : book_.stp_cancelled_ids()) {
        result.cancelled_ids.push_back(stp_id);
    }

    // ── Step 5b: Check dynamic band breach BEFORE updating ref price
    if (!result.fills.empty() && last_traded_price_ > 0 &&
        state_machine_.state() == InstrumentState::Continuous &&
        breaches_dynamic_band(last_traded_price_)) {
        (void)trigger_volatility_auction();
    }

    // ── Step 5c: Update reference price after trades ──────
    if (!result.fills.empty() && last_traded_price_ > 0) {
        config_.static_ref_price = last_traded_price_;
    }

    // ── Step 5d: Check stop order triggers ────────────────
    if (!result.fills.empty() && last_traded_price_ > 0) {
        check_stop_triggers(last_traded_price_);
    }

    // ── Step 5e: Reprice pegged orders after BBO change ──
    if (!result.fills.empty()) {
        reprice_pegged_orders();
    }

    // ── Step 6: Determine result ────────────────────────────
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

    // Check stop queue first
    if (stop_queue_.remove(order_id)) {
        result.action = MatchResult::Action::Cancelled;
        result.cancelled_ids.push_back(order_id);
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
// Closing Auction
// ═══════════════════════════════════════════════════════════════

MatchResult MatchingEngine::run_closing_auction() {
    MatchResult result;

    auto [auction_price, auction_volume] = calculate_auction_price();

    if (auction_price == 0 || auction_volume == 0) {
        (void)state_machine_.transition(InstrumentState::Closed);
        result.action = MatchResult::Action::Inserted;
        return result;
    }

    fill_capture_.result = &result;
    fill_capture_.trade_counter = &next_trade_id_;

    Quantity remaining_volume = auction_volume;

    std::vector<Order*> matchable_bids;
    std::vector<Order*> matchable_asks;

    book_.for_each_order([&](const Order& o) {
        if (o.side == Side::Buy && o.price >= auction_price) {
            matchable_bids.push_back(const_cast<Order*>(&o));
        } else if (o.side == Side::Sell && o.price <= auction_price) {
            matchable_asks.push_back(const_cast<Order*>(&o));
        }
    });

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

        MatchResult::Fill fill;
        fill.trade_id = next_trade_id_++;
        fill.buy_order_id = bid->order_id;
        fill.sell_order_id = ask->order_id;
        fill.fill_price = auction_price;
        fill.fill_qty = fill_qty;
        fill.aggressor_side = Side::Buy;
        result.fills.push_back(fill);

        bid->remaining_qty -= fill_qty;
        bid->cumulative_filled += fill_qty;
        ask->remaining_qty -= fill_qty;
        ask->cumulative_filled += fill_qty;
        remaining_volume -= fill_qty;

        if (bid->remaining_qty == 0) { pool_.release(bid); ++bid_idx; }
        if (ask->remaining_qty == 0) { pool_.release(ask); ++ask_idx; }
    }

    for (size_t i = bid_idx; i < matchable_bids.size(); ++i) {
        if (matchable_bids[i]->remaining_qty > 0) book_.insert_without_matching(matchable_bids[i]);
        else pool_.release(matchable_bids[i]);
    }
    for (size_t i = ask_idx; i < matchable_asks.size(); ++i) {
        if (matchable_asks[i]->remaining_qty > 0) book_.insert_without_matching(matchable_asks[i]);
        else pool_.release(matchable_asks[i]);
    }

    fill_capture_.result = nullptr;
    fill_capture_.trade_counter = nullptr;

    (void)state_machine_.transition(InstrumentState::Closed);

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

    // Stop order validation
    if (order.order_type == OrderType::Stop || order.order_type == OrderType::StopLimit) {
        if (order.stop_price == 0) return RejectCode::StopPriceMissing;
        if (!is_tick_aligned(order.stop_price)) return RejectCode::StopPriceInvalid;
        if (!is_within_static_band(order.stop_price)) return RejectCode::StopPriceInvalid;
        if (order.order_type == OrderType::StopLimit) {
            if (order.price <= 0) return RejectCode::PriceOutsideBands;
            if (!is_tick_aligned(order.price)) return RejectCode::InvalidTick;
            if (!is_within_static_band(order.price)) return RejectCode::PriceOutsideBands;
        }
        return RejectCode::None;  // Skip regular price checks for stop orders
    }

    // Pegged orders: skip price checks (price computed from BBO after validation)
    if (order.peg_type != PegType::None) {
        if (order.order_type != OrderType::Limit) return RejectCode::PegNotLimitOrder;
        return RejectCode::None;
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
        // Dynamic band check (uses last traded price as reference if available)
        if (last_traded_price_ > 0 && config_.dynamic_band_pct > 0.0) {
            double deviation = std::abs(static_cast<double>(order.price - last_traded_price_))
                               / static_cast<double>(last_traded_price_) * 100.0;
            if (deviation > config_.dynamic_band_pct) {
                return RejectCode::PriceOutsideBands;
            }
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

            // Use actual last traded price (tracked via FillCapture listener)
            Price last_fill = last_traded_price_;
            if (last_fill <= 0) last_fill = book_.best_ask();
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

// ═══════════════════════════════════════════════════════════════
// Stop Order Trigger Checking
// ═══════════════════════════════════════════════════════════════

void MatchingEngine::check_stop_triggers(Price last_trade_price) {
    auto triggered_buys = stop_queue_.find_triggered_buys(last_trade_price);
    for (Order* stop : triggered_buys) {
        stop_queue_.remove(stop->order_id);
        activate_stop(stop);
    }

    auto triggered_sells = stop_queue_.find_triggered_sells(last_trade_price);
    for (Order* stop : triggered_sells) {
        stop_queue_.remove(stop->order_id);
        activate_stop(stop);
    }
}

MatchResult MatchingEngine::activate_stop(Order* stop_order) {
    if (stop_order->order_type == OrderType::Stop) {
        stop_order->order_type = OrderType::Market;
        stop_order->price = stop_order->is_buy() ? INT64_MAX : 1;
    } else {
        // StopLimit: keep the limit price already set
        stop_order->order_type = OrderType::Limit;
    }
    return process_new_order(stop_order);
}

// ═══════════════════════════════════════════════════════════════
// Dynamic Band Check
// ═══════════════════════════════════════════════════════════════

bool MatchingEngine::breaches_dynamic_band(Price price) const noexcept {
    if (config_.static_ref_price <= 0 || config_.dynamic_band_pct <= 0.0) return false;
    double deviation = std::abs(static_cast<double>(price - config_.static_ref_price))
                       / static_cast<double>(config_.static_ref_price) * 100.0;
    return deviation > config_.dynamic_band_pct;
}

// ═══════════════════════════════════════════════════════════════
// Volatility Auction (#14)
// ═══════════════════════════════════════════════════════════════

bool MatchingEngine::trigger_volatility_auction() {
    return state_machine_.transition(InstrumentState::Auction);
}

bool MatchingEngine::trigger_scheduled_auction() {
    return state_machine_.transition(InstrumentState::Auction);
}

MatchResult MatchingEngine::run_volatility_auction() {
    MatchResult result;
    auto [auction_price, auction_volume] = calculate_auction_price();
    if (auction_price == 0 || auction_volume == 0) {
        (void)state_machine_.transition(InstrumentState::Continuous);
        result.action = MatchResult::Action::Inserted;
        return result;
    }
    fill_capture_.result = &result;
    fill_capture_.trade_counter = &next_trade_id_;

    Quantity remaining_volume = auction_volume;
    std::vector<Order*> matchable_bids, matchable_asks;
    book_.for_each_order([&](const Order& o) {
        if (o.side == Side::Buy && o.price >= auction_price)
            matchable_bids.push_back(const_cast<Order*>(&o));
        else if (o.side == Side::Sell && o.price <= auction_price)
            matchable_asks.push_back(const_cast<Order*>(&o));
    });
    for (Order* o : matchable_bids) book_.cancel_order(o->order_id);
    for (Order* o : matchable_asks) book_.cancel_order(o->order_id);

    size_t bi = 0, ai = 0;
    while (remaining_volume > 0 && bi < matchable_bids.size() && ai < matchable_asks.size()) {
        Order* bid = matchable_bids[bi];
        Order* ask = matchable_asks[ai];
        Quantity fq = std::min({remaining_volume, bid->remaining_qty, ask->remaining_qty});
        MatchResult::Fill f;
        f.trade_id = next_trade_id_++; f.buy_order_id = bid->order_id;
        f.sell_order_id = ask->order_id; f.fill_price = auction_price;
        f.fill_qty = fq; f.aggressor_side = Side::Buy;
        result.fills.push_back(f);
        bid->remaining_qty -= fq; bid->cumulative_filled += fq;
        ask->remaining_qty -= fq; ask->cumulative_filled += fq;
        remaining_volume -= fq;
        if (bid->remaining_qty == 0) { pool_.release(bid); ++bi; }
        if (ask->remaining_qty == 0) { pool_.release(ask); ++ai; }
    }
    for (size_t i = bi; i < matchable_bids.size(); ++i) {
        if (matchable_bids[i]->remaining_qty > 0) book_.insert_without_matching(matchable_bids[i]);
        else pool_.release(matchable_bids[i]);
    }
    for (size_t i = ai; i < matchable_asks.size(); ++i) {
        if (matchable_asks[i]->remaining_qty > 0) book_.insert_without_matching(matchable_asks[i]);
        else pool_.release(matchable_asks[i]);
    }
    fill_capture_.result = nullptr;
    fill_capture_.trade_counter = nullptr;
    (void)state_machine_.transition(InstrumentState::Continuous);
    result.action = result.fills.empty() ? MatchResult::Action::Inserted : MatchResult::Action::Filled;
    return result;
}

// ═══════════════════════════════════════════════════════════════
// Auction Imbalance (#16)
// ═══════════════════════════════════════════════════════════════

AuctionImbalance MatchingEngine::calculate_auction_imbalance() const {
    AuctionImbalance imb{};
    auto [price, volume] = calculate_auction_price();
    imb.indicative_price = price;
    imb.paired_qty = volume;
    if (price == 0) return imb;

    Quantity cum_bid = 0, cum_ask = 0;
    book_.for_each_order([&](const Order& o) {
        if (o.side == Side::Buy && o.price >= price) cum_bid += o.remaining_qty;
        else if (o.side == Side::Sell && o.price <= price) cum_ask += o.remaining_qty;
    });
    if (cum_bid > cum_ask) {
        imb.imbalance_side = Side::Buy;
        imb.imbalance_qty = cum_bid - cum_ask;
    } else {
        imb.imbalance_side = Side::Sell;
        imb.imbalance_qty = cum_ask - cum_bid;
    }
    return imb;
}

// ═══════════════════════════════════════════════════════════════
// Mass Quote (#8)
// ═══════════════════════════════════════════════════════════════

MassQuoteResult MatchingEngine::process_mass_quote(
        ParticipantId participant, OrderId bid_id, OrderId ask_id,
        Price bid_price, Quantity bid_qty,
        Price ask_price, Quantity ask_qty) {
    MassQuoteResult mqr{};
    mqr.accepted = false;

    // Validate both sides
    if (!is_tick_aligned(bid_price) || !is_tick_aligned(ask_price)) {
        mqr.reject_code = RejectCode::InvalidTick; return mqr;
    }
    if (!is_within_static_band(bid_price) || !is_within_static_band(ask_price)) {
        mqr.reject_code = RejectCode::PriceOutsideBands; return mqr;
    }
    if (bid_price >= ask_price) {
        mqr.reject_code = RejectCode::PriceOutsideBands; return mqr;
    }
    if (bid_qty < config_.min_quantity || ask_qty < config_.min_quantity) {
        mqr.reject_code = RejectCode::InvalidLot; return mqr;
    }

    // Cancel existing quotes for this participant
    cancel_participant_quotes(participant);

    // Insert bid
    Order* bid = pool_.allocate();
    if (!bid) { mqr.reject_code = RejectCode::RiskCheckFailed; return mqr; }
    bid->clear();
    bid->order_id = bid_id; bid->instrument_id = config_.instrument_id;
    bid->participant_id = participant; bid->side = Side::Buy;
    bid->order_type = OrderType::Limit; bid->time_in_force = TimeInForce::Day;
    bid->price = bid_price; bid->original_qty = bid_qty; bid->remaining_qty = bid_qty;
    book_.insert_without_matching(bid);

    // Insert ask
    Order* ask = pool_.allocate();
    if (!ask) { book_.cancel_order(bid_id); pool_.release(bid); mqr.reject_code = RejectCode::RiskCheckFailed; return mqr; }
    ask->clear();
    ask->order_id = ask_id; ask->instrument_id = config_.instrument_id;
    ask->participant_id = participant; ask->side = Side::Sell;
    ask->order_type = OrderType::Limit; ask->time_in_force = TimeInForce::Day;
    ask->price = ask_price; ask->original_qty = ask_qty; ask->remaining_qty = ask_qty;
    book_.insert_without_matching(ask);

    participant_quotes_[participant] = {bid_id, ask_id};
    mqr.accepted = true;
    mqr.bid_order_id = bid_id;
    mqr.ask_order_id = ask_id;
    return mqr;
}

void MatchingEngine::cancel_participant_quotes(ParticipantId participant) {
    auto it = participant_quotes_.find(participant);
    if (it == participant_quotes_.end()) return;
    auto [bid_id, ask_id] = it->second;
    Order* bid = book_.find_order(bid_id);
    if (bid) { book_.cancel_order(bid_id); pool_.release(bid); }
    Order* ask = book_.find_order(ask_id);
    if (ask) { book_.cancel_order(ask_id); pool_.release(ask); }
    participant_quotes_.erase(it);
}

// ═══════════════════════════════════════════════════════════════
// Pegged Orders (#4)
// ═══════════════════════════════════════════════════════════════

Price MatchingEngine::calculate_peg_price(const Order& order) const {
    Price bid = book_.best_bid();
    Price ask = book_.best_ask();
    Price ref = 0;

    switch (order.peg_type) {
        case PegType::Primary:
            ref = order.is_buy() ? bid : ask;
            break;
        case PegType::Midpoint:
            if (bid > 0 && ask > 0) ref = (bid + ask) / 2;
            break;
        case PegType::Market:
            ref = order.is_buy() ? ask : bid;
            break;
        case PegType::None:
            return order.price;
    }

    if (ref <= 0) return 0;  // No reference available

    Price result = ref + order.peg_offset;
    if (result <= 0) return 0;

    // Apply peg limit (cap)
    if (order.peg_limit > 0) {
        if (order.is_buy() && result > order.peg_limit) result = order.peg_limit;
        if (order.is_sell() && result < order.peg_limit) result = order.peg_limit;
    }

    return result;
}

void MatchingEngine::reprice_pegged_orders() {
    // Collect pegged orders that need repricing
    struct RepricePair { OrderId id; Price new_price; };
    std::vector<RepricePair> to_reprice;

    book_.for_each_order([&](const Order& o) {
        if (o.peg_type != PegType::None) {
            Price new_price = calculate_peg_price(o);
            if (new_price > 0 && new_price != o.price) {
                to_reprice.push_back({o.order_id, new_price});
            }
        }
    });

    // Apply repricing (modify_order changes price and loses time priority)
    for (const auto& rp : to_reprice) {
        book_.modify_order(rp.id, rp.new_price, 0);
    }
}

}  // namespace engine
