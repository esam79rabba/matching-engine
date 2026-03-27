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

// ─── Sliding Window Specific Tests ────────────────────────────

TEST(RateLimiterTest, SlidingWindowBasic) {
    RateLimiter limiter;
    limiter.set_defaults({.max_messages_per_window = 5, .window_ms = 1000});

    // Submit exactly max_per_second requests — all should pass
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(limiter.check_and_increment(1, (100 + i * 100) * MS));
    }
    // The 6th should fail (still within 1s window)
    EXPECT_FALSE(limiter.check_and_increment(1, 600 * MS));
}

TEST(RateLimiterTest, SlidingWindowRecovery) {
    RateLimiter limiter;
    limiter.set_defaults({.max_messages_per_window = 3, .window_ms = 1000});

    // Fill the window: messages at t=100ms, t=200ms, t=300ms
    EXPECT_TRUE(limiter.check_and_increment(1, 100 * MS));
    EXPECT_TRUE(limiter.check_and_increment(1, 200 * MS));
    EXPECT_TRUE(limiter.check_and_increment(1, 300 * MS));
    EXPECT_FALSE(limiter.check_and_increment(1, 400 * MS));  // Full

    // Wait 1 second after the first message (t=1100ms).
    // The message at t=100ms should have expired from the window.
    // Window now contains: t=200ms, t=300ms → room for 1 more
    EXPECT_TRUE(limiter.check_and_increment(1, 1100 * MS));
    EXPECT_FALSE(limiter.check_and_increment(1, 1150 * MS)); // Full again (200,300,1100)
}

TEST(RateLimiterTest, SlidingWindowNoBurst) {
    // With a fixed-window limiter, you can burst 2x the rate at window edges:
    // send max at end of window, then max at start of next window.
    // A sliding window prevents this.
    RateLimiter limiter;
    limiter.set_defaults({.max_messages_per_window = 3, .window_ms = 1000});

    // Send 3 messages near end of a "window" (t=900ms, 950ms, 990ms)
    EXPECT_TRUE(limiter.check_and_increment(1, 900 * MS));
    EXPECT_TRUE(limiter.check_and_increment(1, 950 * MS));
    EXPECT_TRUE(limiter.check_and_increment(1, 990 * MS));

    // At t=1010ms (just 20ms later), with sliding window, the oldest (t=900ms)
    // is still within the 1-second window (1010-900=110ms < 1000ms).
    // So all 3 are still active and this should be rejected.
    EXPECT_FALSE(limiter.check_and_increment(1, 1010 * MS));

    // At t=1050ms, still all 3 within window (1050-900=150ms < 1000ms)
    EXPECT_FALSE(limiter.check_and_increment(1, 1050 * MS));

    // At t=1901ms, the first (t=900ms) expires (1901-900=1001ms >= 1000ms)
    EXPECT_TRUE(limiter.check_and_increment(1, 1901 * MS));
}

}  // namespace
}  // namespace risk
