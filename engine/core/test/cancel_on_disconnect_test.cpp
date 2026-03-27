#include "engine/cancel_on_disconnect.h"
#include <gtest/gtest.h>

namespace engine {
namespace {

TEST(CoDTest, DisabledSessionReturnsEmpty) {
    CancelOnDisconnect cod;
    cod.configure_session(1, {.enabled = false});
    cod.track_order(1, 100);
    auto orders = cod.on_disconnect(1);
    EXPECT_TRUE(orders.empty());
}

TEST(CoDTest, EnabledSessionReturnsCancels) {
    CancelOnDisconnect cod;
    cod.configure_session(1, {.enabled = true});
    cod.track_order(1, 100);
    cod.track_order(1, 101);
    cod.track_order(1, 102);

    auto orders = cod.on_disconnect(1);
    EXPECT_EQ(orders.size(), 3u);
    // Sorted for determinism
    EXPECT_EQ(orders[0], 100u);
    EXPECT_EQ(orders[1], 101u);
    EXPECT_EQ(orders[2], 102u);
}

TEST(CoDTest, UntrackedOrderNotReturned) {
    CancelOnDisconnect cod;
    cod.configure_session(1, {.enabled = true});
    cod.track_order(1, 100);
    cod.track_order(1, 101);
    cod.untrack_order(100);  // Filled or cancelled

    auto orders = cod.on_disconnect(1);
    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0], 101u);
}

TEST(CoDTest, DifferentSessionsIndependent) {
    CancelOnDisconnect cod;
    cod.configure_session(1, {.enabled = true});
    cod.configure_session(2, {.enabled = true});
    cod.track_order(1, 100);
    cod.track_order(2, 200);

    auto orders1 = cod.on_disconnect(1);
    ASSERT_EQ(orders1.size(), 1u);
    EXPECT_EQ(orders1[0], 100u);

    // Session 2 still has its order
    EXPECT_EQ(cod.order_count(2), 1u);
}

TEST(CoDTest, DisconnectClearsTracking) {
    CancelOnDisconnect cod;
    cod.configure_session(1, {.enabled = true});
    cod.track_order(1, 100);

    (void)cod.on_disconnect(1);
    EXPECT_EQ(cod.order_count(1), 0u);

    // Second disconnect returns empty
    auto orders = cod.on_disconnect(1);
    EXPECT_TRUE(orders.empty());
}

TEST(CoDTest, UnconfiguredSessionReturnsEmpty) {
    CancelOnDisconnect cod;
    cod.track_order(99, 500);  // No config for session 99
    auto orders = cod.on_disconnect(99);
    EXPECT_TRUE(orders.empty());
}

TEST(CoDTest, IsEnabledQuery) {
    CancelOnDisconnect cod;
    cod.configure_session(1, {.enabled = true});
    cod.configure_session(2, {.enabled = false});
    EXPECT_TRUE(cod.is_enabled(1));
    EXPECT_FALSE(cod.is_enabled(2));
    EXPECT_FALSE(cod.is_enabled(99));
}

}  // namespace
}  // namespace engine
