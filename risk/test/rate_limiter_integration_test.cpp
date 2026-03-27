#include "risk/risk_service.h"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

namespace risk {
namespace {

RiskRequest make_request(engine::SessionId session = 1) {
    RiskRequest req{};
    req.client_order_id = 1;
    req.instrument_id = 100;
    req.participant_id = 1;
    req.account_id = 1;
    req.session_id = session;
    req.side = engine::Side::Buy;
    req.order_type = engine::OrderType::Limit;
    req.price = 10000;
    req.quantity = 100;
    req.reference_price = 10000;
    req.price_multiplier = 100;
    return req;
}

// ─── Rate limiter wired into BasicRiskService ─────────────────

TEST(RateLimiterIntegrationTest, RateLimiterRejectsWhenExceeded) {
    // Configure: max 5 messages per 1000ms window
    BasicRiskService svc(/*max_messages_per_second=*/5, /*window_ms=*/1000);

    // First 5 should pass
    for (int i = 0; i < 5; ++i) {
        auto resp = svc.check(make_request());
        EXPECT_EQ(resp.result, RiskResult::PASS)
            << "Message " << i << " should pass";
    }

    // 6th message should be rate-limited
    auto resp = svc.check(make_request());
    EXPECT_EQ(resp.result, RiskResult::REJECT);
    EXPECT_EQ(resp.reject_code, engine::RejectCode::RateLimitExceeded);
}

TEST(RateLimiterIntegrationTest, RateLimiterDisabledByDefault) {
    // Default constructor: rate limiting disabled (max_messages_per_second = 0)
    BasicRiskService svc;

    // Send many messages — all should pass (rate limiter disabled)
    for (int i = 0; i < 100; ++i) {
        auto resp = svc.check(make_request());
        EXPECT_EQ(resp.result, RiskResult::PASS)
            << "Message " << i << " should pass when rate limiter is disabled";
    }
}

TEST(RateLimiterIntegrationTest, RateLimiterPerSession) {
    // Configure: max 3 messages per 1000ms window
    BasicRiskService svc(/*max_messages_per_second=*/3, /*window_ms=*/1000);

    // Session 1: send 3 messages (exhaust limit)
    for (int i = 0; i < 3; ++i) {
        auto resp = svc.check(make_request(/*session=*/1));
        EXPECT_EQ(resp.result, RiskResult::PASS);
    }

    // Session 1: 4th message should be rejected
    auto resp1 = svc.check(make_request(/*session=*/1));
    EXPECT_EQ(resp1.result, RiskResult::REJECT);
    EXPECT_EQ(resp1.reject_code, engine::RejectCode::RateLimitExceeded);

    // Session 2: should still have its own budget
    for (int i = 0; i < 3; ++i) {
        auto resp = svc.check(make_request(/*session=*/2));
        EXPECT_EQ(resp.result, RiskResult::PASS)
            << "Session 2 message " << i << " should pass independently";
    }

    // Session 2: 4th message should also be rejected
    auto resp2 = svc.check(make_request(/*session=*/2));
    EXPECT_EQ(resp2.result, RiskResult::REJECT);
    EXPECT_EQ(resp2.reject_code, engine::RejectCode::RateLimitExceeded);
}

}  // namespace
}  // namespace risk
