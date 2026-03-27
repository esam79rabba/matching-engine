#include "surveillance/rules/wash_trade_rule.h"
#include "surveillance/rules/layering_rule.h"
#include "surveillance/rules/order_trade_ratio_rule.h"
#include "surveillance/rules/quote_stuffing_rule.h"
#include <gtest/gtest.h>

namespace surveillance {
namespace {

// ═══════════════════════════════════════════════════════════
// Wash Trade Rule Unit Tests
// ═══════════════════════════════════════════════════════════

TEST(WashTradeRuleTest, DetectsSameParticipantTrade) {
    WashTradeRule rule;
    rule.on_trade(/*buyer=*/1, /*seller=*/1, 100, 10000, 50, 1000);
    auto alerts = rule.check();
    ASSERT_EQ(alerts.size(), 1u);
    EXPECT_EQ(alerts[0].severity, AlertSeverity::Critical);
    EXPECT_EQ(alerts[0].participant_id, 1u);
}

TEST(WashTradeRuleTest, NormalTradeNoAlert) {
    WashTradeRule rule;
    rule.on_trade(1, 2, 100, 10000, 50, 1000);
    auto alerts = rule.check();
    EXPECT_TRUE(alerts.empty());
}

TEST(WashTradeRuleTest, MultipleWashTrades) {
    WashTradeRule rule;
    rule.on_trade(1, 1, 100, 10000, 50, 1000);
    rule.on_trade(2, 2, 100, 10000, 50, 2000);
    auto alerts = rule.check();
    EXPECT_EQ(alerts.size(), 2u);
}

TEST(WashTradeRuleTest, CheckClearsAlerts) {
    WashTradeRule rule;
    rule.on_trade(1, 1, 100, 10000, 50, 1000);
    auto a1 = rule.check();
    EXPECT_EQ(a1.size(), 1u);
    auto a2 = rule.check();
    EXPECT_TRUE(a2.empty());
}

// ═══════════════════════════════════════════════════════════
// Layering Rule Unit Tests
// ═══════════════════════════════════════════════════════════

TEST(LayeringRuleTest, HighCancelRatioTriggersAlert) {
    LayeringRule rule;
    // 10 orders, 9 cancels = 90% cancel ratio
    for (int i = 0; i < 10; i++)
        rule.on_order(1, 100, engine::Side::Buy, 10000, 50, 1000 + i);
    for (int i = 0; i < 9; i++)
        rule.on_cancel(1, 100, 1000 + i);
    auto alerts = rule.check();
    EXPECT_GE(alerts.size(), 1u);
    if (!alerts.empty()) {
        EXPECT_EQ(alerts[0].severity, AlertSeverity::High);
    }
}

TEST(LayeringRuleTest, LowCancelRatioNoAlert) {
    LayeringRule rule;
    // 10 orders, 2 cancels = 20% cancel ratio
    for (int i = 0; i < 10; i++)
        rule.on_order(1, 100, engine::Side::Buy, 10000, 50, 1000 + i);
    for (int i = 0; i < 2; i++)
        rule.on_cancel(1, 100, 1000 + i);
    auto alerts = rule.check();
    EXPECT_TRUE(alerts.empty());
}

// ═══════════════════════════════════════════════════════════
// Order-to-Trade Ratio Unit Tests
// ═══════════════════════════════════════════════════════════

TEST(OrderToTradeRatioTest, HighRatioTriggersAlert) {
    OrderToTradeRatioRule rule;
    // 200 orders, 1 trade = 200:1 ratio
    for (int i = 0; i < 200; i++)
        rule.on_order(1, 100, engine::Side::Buy, 10000, 50, 1000 + i);
    rule.on_trade(1, 2, 100, 10000, 50, 2000);
    auto alerts = rule.check();
    EXPECT_GE(alerts.size(), 1u);
    if (!alerts.empty()) {
        EXPECT_EQ(alerts[0].severity, AlertSeverity::Medium);
    }
}

TEST(OrderToTradeRatioTest, NormalRatioNoAlert) {
    OrderToTradeRatioRule rule;
    // 5 orders, 3 trades = ~1.7:1 ratio
    for (int i = 0; i < 5; i++)
        rule.on_order(1, 100, engine::Side::Buy, 10000, 50, 1000 + i);
    for (int i = 0; i < 3; i++)
        rule.on_trade(1, 2, 100, 10000, 50, 2000 + i);
    auto alerts = rule.check();
    EXPECT_TRUE(alerts.empty());
}

// ═══════════════════════════════════════════════════════════
// Quote Stuffing Unit Tests
// ═══════════════════════════════════════════════════════════

TEST(QuoteStuffingRuleTest, HighRateTriggersAlert) {
    QuoteStuffingRule rule;
    // 600 orders in 1 second = way above threshold
    for (int i = 0; i < 600; i++)
        rule.on_order(1, 100, engine::Side::Buy, 10000, 50, 1000);  // Same timestamp
    auto alerts = rule.check();
    EXPECT_GE(alerts.size(), 1u);
    if (!alerts.empty()) {
        EXPECT_EQ(alerts[0].severity, AlertSeverity::High);
    }
}

TEST(QuoteStuffingRuleTest, NormalRateNoAlert) {
    QuoteStuffingRule rule;
    // 5 orders spread out
    for (int i = 0; i < 5; i++)
        rule.on_order(1, 100, engine::Side::Buy, 10000, 50, 1000000000ULL * (i + 1));
    auto alerts = rule.check();
    EXPECT_TRUE(alerts.empty());
}

// ═══════════════════════════════════════════════════════════
// Rule Name Tests
// ═══════════════════════════════════════════════════════════

TEST(RuleNamesTest, AllRulesHaveNames) {
    WashTradeRule wt;
    LayeringRule lr;
    OrderToTradeRatioRule otr;
    QuoteStuffingRule qs;

    EXPECT_FALSE(wt.name().empty());
    EXPECT_FALSE(lr.name().empty());
    EXPECT_FALSE(otr.name().empty());
    EXPECT_FALSE(qs.name().empty());
}

}  // namespace
}  // namespace surveillance
