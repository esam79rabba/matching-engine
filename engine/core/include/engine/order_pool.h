#pragma once

#include "engine/order.h"
#include <cstdint>
#include <vector>

namespace engine {

/// Pre-allocated pool of Order objects for zero-allocation hot-path operation.
///
/// Design notes (generic core — no asset-class assumptions):
///   - All Order memory allocated at construction time.
///   - allocate() and release() are O(1) — free-list stack.
///   - Single-threaded per partition — no synchronization needed.
///   - The pool does not know or care what instrument type an order belongs to.
///     Asset-class-specific fields (precision, settlement model, etc.) live in
///     reference data, not in the order or pool.
class OrderPool {
public:
    /// Construct a pool with `capacity` pre-allocated Order objects.
    explicit OrderPool(uint32_t capacity);

    /// Allocate an order from the pool. Returns nullptr if pool is exhausted.
    /// The returned Order has clear() already called.
    [[nodiscard]] Order* allocate() noexcept;

    /// Release an order back to the pool for reuse.
    void release(Order* order) noexcept;

    /// Number of orders currently in use (allocated but not released).
    [[nodiscard]] uint32_t active_count() const noexcept { return active_count_; }

    /// Total pool capacity.
    [[nodiscard]] uint32_t capacity() const noexcept { return capacity_; }

    /// Number of available slots in the pool.
    [[nodiscard]] uint32_t available() const noexcept { return capacity_ - active_count_; }

    // Non-copyable, non-movable (owns raw memory)
    OrderPool(const OrderPool&) = delete;
    OrderPool& operator=(const OrderPool&) = delete;
    OrderPool(OrderPool&&) = delete;
    OrderPool& operator=(OrderPool&&) = delete;

private:
    uint32_t capacity_;
    uint32_t active_count_ = 0;

    // Contiguous storage for all Order objects.
    std::vector<Order> storage_;

    // Free-list implemented as a stack of pointers into storage_.
    std::vector<Order*> free_list_;
};

}  // namespace engine
