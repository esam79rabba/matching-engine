#include "engine/order_pool.h"
#include <gtest/gtest.h>

namespace engine {
namespace {

TEST(OrderPoolTest, InitialStateHasFullCapacity) {
    OrderPool pool(1000);
    EXPECT_EQ(pool.capacity(), 1000u);
    EXPECT_EQ(pool.active_count(), 0u);
    EXPECT_EQ(pool.available(), 1000u);
}

TEST(OrderPoolTest, AllocateReturnsCleanOrder) {
    OrderPool pool(10);
    Order* o = pool.allocate();
    ASSERT_NE(o, nullptr);
    EXPECT_EQ(o->order_id, 0u);
    EXPECT_EQ(o->remaining_qty, 0u);
    EXPECT_EQ(pool.active_count(), 1u);
    EXPECT_EQ(pool.available(), 9u);
}

TEST(OrderPoolTest, ReleaseReturnsOrderToPool) {
    OrderPool pool(10);
    Order* o = pool.allocate();
    pool.release(o);
    EXPECT_EQ(pool.active_count(), 0u);
    EXPECT_EQ(pool.available(), 10u);
}

TEST(OrderPoolTest, ExhaustedPoolReturnsNullptr) {
    OrderPool pool(2);
    Order* o1 = pool.allocate();
    Order* o2 = pool.allocate();
    Order* o3 = pool.allocate();
    EXPECT_NE(o1, nullptr);
    EXPECT_NE(o2, nullptr);
    EXPECT_EQ(o3, nullptr);  // Pool exhausted
    EXPECT_EQ(pool.active_count(), 2u);
}

TEST(OrderPoolTest, ReleaseNullptrIsSafe) {
    OrderPool pool(10);
    pool.release(nullptr);  // Should not crash
    EXPECT_EQ(pool.active_count(), 0u);
}

TEST(OrderPoolTest, AllocateAfterReleaseReusesMemory) {
    OrderPool pool(1);
    Order* o1 = pool.allocate();
    o1->order_id = 42;
    pool.release(o1);

    Order* o2 = pool.allocate();
    EXPECT_EQ(o2, o1);          // Same pointer reused
    EXPECT_EQ(o2->order_id, 0u); // But cleared
}

}  // namespace
}  // namespace engine
