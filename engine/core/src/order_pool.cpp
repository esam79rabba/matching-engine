#include "engine/order_pool.h"

namespace engine {

OrderPool::OrderPool(uint32_t capacity)
    : capacity_(capacity) {
    storage_.resize(capacity);
    free_list_.reserve(capacity);

    // Initialize free list — all orders available
    for (uint32_t i = 0; i < capacity; ++i) {
        storage_[i].clear();
        free_list_.push_back(&storage_[i]);
    }
}

Order* OrderPool::allocate() noexcept {
    if (free_list_.empty()) {
        return nullptr;  // Pool exhausted
    }
    Order* order = free_list_.back();
    free_list_.pop_back();
    order->clear();
    ++active_count_;
    return order;
}

void OrderPool::release(Order* order) noexcept {
    if (order == nullptr) return;
    order->clear();
    free_list_.push_back(order);
    --active_count_;
}

}  // namespace engine
