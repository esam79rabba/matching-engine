#include <gtest/gtest.h>

#include <surveillance/surveillance_engine.h>
#include <surveillance/rules/wash_trade_rule.h>
#include <surveillance/rules/layering_rule.h>
#include <surveillance/rules/order_trade_ratio_rule.h>
#include <surveillance/rules/quote_stuffing_rule.h>

using namespace surveillance;

// ─── Helpers ──────────────────────────────────────────────────

static constexpr engine::InstrumentId INST = 1;
static constexpr engine::Price PRICE = 10000;
static constexpr engine::Quantity QTY = 100;
static constexpr uint64_t SEC = 1'000'000'000ULL;  // 1 second in nanos

// ─── WashTradeRule Tests ──────────────────────────────────────

TEST(SurveillanceWashTrade, WashTradeDetected) {
    WashTradeRule rule;
    engine::ParticipantId participant = 42;

    // Same participant is both buyer and seller
    rule.on_trade(participant, participant, INST, PRICE, QTY, 1 * SEC);

    auto alerts = rule.check();
    ASSERT_EQ(alerts.size(), 1u);
    EXPECT_EQ(alerts[0].severity, AlertSeverity::Critical);
    EXPECT_EQ(alerts[0].rule_name, "WashTrade");
    EXPECT_EQ(alerts[0].participant_id, participant);
    EXPECT_EQ(alerts[0].instrument_id, INST);
}

TEST(SurveillanceWashTrade, NoWashTradeForDifferentParticipants) {
    WashTradeRule rule;

    // Different participants — no wash trade
    rule.on_trade(1, 2, INST, PRICE, QTY, 1 * SEC);

    auto alerts = rule.check();
    EXPECT_TRUE(alerts.empty());
}

// ─── LayeringRule Tests ───────────────────────────────────────

TEST(SurveillanceLayering, LayeringDetected) {
    // Use small window and low minimum for testability
    LayeringRule rule(10 * SEC, 0.80);

    engine::ParticipantId participant = 10;
    uint64_t base_time = 100 * SEC;

    // Submit 10 orders, cancel 9 of them (90% cancel ratio)
    for (int i = 0; i < 10; ++i) {
        rule.on_order(participant, INST, engine::Side::Buy,
                      PRICE, QTY, base_time + i);
    }
    for (int i = 0; i < 9; ++i) {
        rule.on_cancel(participant, INST, base_time + 10 + i);
    }

    auto alerts = rule.check();
    ASSERT_GE(alerts.size(), 1u);
    EXPECT_EQ(alerts[0].severity, AlertSeverity::High);
    EXPECT_EQ(alerts[0].rule_name, "Layering");
    EXPECT_EQ(alerts[0].participant_id, participant);
}

// ─── OrderToTradeRatioRule Tests ──────────────────────────────

TEST(SurveillanceOTR, OrderToTradeRatioAlert) {
    // Low threshold for testing: ratio > 5:1 triggers
    OrderToTradeRatioRule rule(60 * SEC, 5.0);

    engine::ParticipantId participant = 20;
    uint64_t base_time = 100 * SEC;

    // Submit 20 orders, only 1 trade (ratio = 20:1)
    for (int i = 0; i < 20; ++i) {
        rule.on_order(participant, INST, engine::Side::Buy,
                      PRICE, QTY, base_time + i);
    }
    // One trade involving this participant
    rule.on_trade(participant, 99, INST, PRICE, QTY, base_time + 20);

    auto alerts = rule.check();
    ASSERT_EQ(alerts.size(), 1u);
    EXPECT_EQ(alerts[0].severity, AlertSeverity::Medium);
    EXPECT_EQ(alerts[0].rule_name, "OrderToTradeRatio");
    EXPECT_EQ(alerts[0].participant_id, participant);
}

// ─── QuoteStuffingRule Tests ──────────────────────────────────

TEST(SurveillanceQuoteStuffing, QuoteStuffingDetected) {
    // Low threshold for testing: > 10 msgs/sec
    QuoteStuffingRule rule(1 * SEC, 10);

    engine::ParticipantId participant = 30;
    uint64_t base_time = 100 * SEC;

    // Send 15 messages within 1 second window
    for (int i = 0; i < 15; ++i) {
        rule.on_order(participant, INST, engine::Side::Buy,
                      PRICE, QTY, base_time + i);
    }

    auto alerts = rule.check();
    ASSERT_EQ(alerts.size(), 1u);
    EXPECT_EQ(alerts[0].severity, AlertSeverity::High);
    EXPECT_EQ(alerts[0].rule_name, "QuoteStuffing");
    EXPECT_EQ(alerts[0].participant_id, participant);
}

// ─── SurveillanceEngine Integration Tests ─────────────────────

TEST(SurveillanceEngine, MultipleRulesCollectAlerts) {
    SurveillanceEngine engine;

    engine.add_rule(std::make_unique<WashTradeRule>());
    engine.add_rule(std::make_unique<QuoteStuffingRule>(1 * SEC, 5));

    EXPECT_EQ(engine.rule_count(), 2u);

    uint64_t base_time = 100 * SEC;
    engine::ParticipantId participant = 50;

    // Trigger wash trade
    engine.on_trade(participant, participant, INST, PRICE, QTY, base_time);

    // Trigger quote stuffing (send 10 messages in 1 sec)
    for (int i = 0; i < 10; ++i) {
        engine.on_order(participant, INST, engine::Side::Sell,
                        PRICE, QTY, base_time + 1 + i);
    }

    auto alerts = engine.collect_alerts();
    // At least 2 alerts: one wash trade + one quote stuffing
    EXPECT_GE(alerts.size(), 2u);

    bool has_wash = false;
    bool has_stuffing = false;
    for (const auto& a : alerts) {
        if (a.rule_name == "WashTrade") has_wash = true;
        if (a.rule_name == "QuoteStuffing") has_stuffing = true;
    }
    EXPECT_TRUE(has_wash);
    EXPECT_TRUE(has_stuffing);
}

TEST(SurveillanceEngine, NoAlertsWhenClean) {
    SurveillanceEngine engine;

    engine.add_rule(std::make_unique<WashTradeRule>());
    engine.add_rule(std::make_unique<LayeringRule>());
    engine.add_rule(std::make_unique<OrderToTradeRatioRule>());
    engine.add_rule(std::make_unique<QuoteStuffingRule>());

    uint64_t base_time = 100 * SEC;

    // Normal trading: different participants, moderate activity
    engine.on_order(1, INST, engine::Side::Buy, PRICE, QTY, base_time);
    engine.on_order(2, INST, engine::Side::Sell, PRICE, QTY, base_time + 1);
    engine.on_trade(1, 2, INST, PRICE, QTY, base_time + 2);

    auto alerts = engine.collect_alerts();
    EXPECT_TRUE(alerts.empty());
}
