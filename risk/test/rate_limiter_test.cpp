#include "risk/rate_limiter.h"
#include <gtest/gtest.h>

namespace risk {
namespace {

constexpr uint64_t MS = 1000000ULL;  // 1ms in nanoseconds

TEST(RateLimiterTest, AllowsWithinLimit) {
    RateLimiter limiter;
    limiter.set_defaults({.max_messages_per_window = 5, .window_ms = 1000});

    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(limiter.check_and_increment(1, 100 * MS));
    }
}

TEST(RateLimiterTest, RejectsOverLimit) {
    RateLimiter limiter;
    limiter.set_defaults({.max_messages_per_window = 3, .window_ms = 1000});

    EXPECT_TRUE(limiter.check_and_increment(1, 100 * MS));
    EXPECT_TRUE(limiter.check_and_increment(1, 200 * MS));
    EXPECT_TRUE(limiter.check_and_increment(1, 300 * MS));
    EXPECT_FALSE(limiter.check_and_increment(1, 400 * MS));  // 4th exceeds limit of 3
    EXPECT_FALSE(limiter.check_and_increment(1, 500 * MS));  // Still in same window
}

TEST(RateLimiterTest, WindowResetAllowsNewMessages) {
    RateLimiter limiter;
    limiter.set_defaults({.max_messages_per_window = 2, .window_ms = 1000});

    EXPECT_TRUE(limiter.check_and_increment(1, 100 * MS));
    EXPECT_TRUE(limiter.check_and_increment(1, 200 * MS));
    EXPECT_FALSE(limiter.check_and_increment(1, 300 * MS));  // Over limit

    // Move to next window (1000ms later)
    EXPECT_TRUE(limiter.check_and_increment(1, 1200 * MS));  // New window
    EXPECT_TRUE(limiter.check_and_increment(1, 1300 * MS));
    EXPECT_FALSE(limiter.check_and_increment(1, 1400 * MS)); // Over again
}

TEST(RateLimiterTest, PerSessionIndependence) {
    RateLimiter limiter;
    limiter.set_defaults({.max_messages_per_window = 2, .window_ms = 1000});

    EXPECT_TRUE(limiter.check_and_increment(1, 100 * MS));
    EXPECT_TRUE(limiter.check_and_increment(1, 200 * MS));
    EXPECT_FALSE(limiter.check_and_increment(1, 300 * MS));  // Session 1 limited

    EXPECT_TRUE(limiter.check_and_increment(2, 300 * MS));   // Session 2 independent
    EXPECT_TRUE(limiter.check_and_increment(2, 400 * MS));
}

TEST(RateLimiterTest, PerSessionConfig) {
    RateLimiter limiter;
    limiter.set_defaults({.max_messages_per_window = 100, .window_ms = 1000});
    limiter.configure(1, {.max_messages_per_window = 2, .window_ms = 1000});

    // Session 1: tight limit
    EXPECT_TRUE(limiter.check_and_increment(1, 100 * MS));
    EXPECT_TRUE(limiter.check_and_increment(1, 200 * MS));
    EXPECT_FALSE(limiter.check_and_increment(1, 300 * MS));

    // Session 2: uses default (100)
    for (int i = 0; i < 50; ++i) {
        EXPECT_TRUE(limiter.check_and_increment(2, (100 + i) * MS));
    }
}

TEST(RateLimiterTest, CurrentCount) {
    RateLimiter limiter;
    limiter.set_defaults({.max_messages_per_window = 100, .window_ms = 1000});

    EXPECT_EQ(limiter.current_count(1), 0u);

    (void)limiter.check_and_increment(1, 100 * MS);
    (void)limiter.check_and_increment(1, 200 * MS);
    EXPECT_EQ(limiter.current_count(1), 2u);
}

TEST(RateLimiterTest, ResetClearsCount) {
    RateLimiter limiter;
    limiter.set_defaults({.max_messages_per_window = 2, .window_ms = 1000});

    (void)limiter.check_and_increment(1, 100 * MS);
    (void)limiter.check_and_increment(1, 200 * MS);
    limiter.reset(1);
    EXPECT_EQ(limiter.current_count(1), 0u);

    // Can send again
    EXPECT_TRUE(limiter.check_and_increment(1, 300 * MS));
}

}  // namespace
}  // namespace risk
