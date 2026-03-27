/// Metrics integration tests — verify that the metric keys used
/// throughout the daemon are properly tracked by the Metrics singleton.
/// Covers #104: missing Prometheus metrics.

#include "ops/metrics.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace ops {
namespace {

class MetricsIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        Metrics::instance().reset();
    }
};

// ─── Engine metrics ─────────────────────────────────────────────

TEST_F(MetricsIntegrationTest, EngineMatchesTotal) {
    Metrics::instance().increment("engine.matches.total");
    Metrics::instance().increment("engine.matches.total");
    EXPECT_EQ(Metrics::instance().get_counter("engine.matches.total"), 2u);

    auto prom = Metrics::instance().to_prometheus();
    EXPECT_NE(prom.find("engine_matches_total"), std::string::npos);
}

TEST_F(MetricsIntegrationTest, EngineOrdersResting) {
    Metrics::instance().set_gauge("engine.orders.resting", 42);
    EXPECT_EQ(Metrics::instance().get_gauge("engine.orders.resting"), 42);

    Metrics::instance().set_gauge("engine.orders.resting", 10);
    EXPECT_EQ(Metrics::instance().get_gauge("engine.orders.resting"), 10);

    auto prom = Metrics::instance().to_prometheus();
    EXPECT_NE(prom.find("engine_orders_resting"), std::string::npos);
}

// ─── Market data metrics ────────────────────────────────────────

TEST_F(MetricsIntegrationTest, MarketDataMessagesPublished) {
    Metrics::instance().increment("market_data.messages_published");
    Metrics::instance().increment("market_data.messages_published");
    Metrics::instance().increment("market_data.messages_published");
    EXPECT_EQ(Metrics::instance().get_counter("market_data.messages_published"), 3u);

    auto prom = Metrics::instance().to_prometheus();
    EXPECT_NE(prom.find("market_data_messages_published"), std::string::npos);
}

// ─── FIX session metrics ────────────────────────────────────────

TEST_F(MetricsIntegrationTest, FixActiveSessions) {
    Metrics::instance().set_gauge("fix.active_sessions", 0);
    EXPECT_EQ(Metrics::instance().get_gauge("fix.active_sessions"), 0);

    // Simulate logon
    Metrics::instance().set_gauge("fix.active_sessions", 1);
    EXPECT_EQ(Metrics::instance().get_gauge("fix.active_sessions"), 1);

    // Simulate second logon
    Metrics::instance().set_gauge("fix.active_sessions", 2);
    EXPECT_EQ(Metrics::instance().get_gauge("fix.active_sessions"), 2);

    // Simulate logout
    Metrics::instance().set_gauge("fix.active_sessions", 1);
    EXPECT_EQ(Metrics::instance().get_gauge("fix.active_sessions"), 1);

    auto prom = Metrics::instance().to_prometheus();
    EXPECT_NE(prom.find("fix_active_sessions"), std::string::npos);
}

// ─── Journal metrics ────────────────────────────────────────────

TEST_F(MetricsIntegrationTest, JournalEntriesWritten) {
    for (int i = 0; i < 5; ++i) {
        Metrics::instance().increment("journal.entries_written");
    }
    EXPECT_EQ(Metrics::instance().get_counter("journal.entries_written"), 5u);

    auto prom = Metrics::instance().to_prometheus();
    EXPECT_NE(prom.find("journal_entries_written"), std::string::npos);
}

// ─── Surveillance metrics ───────────────────────────────────────

TEST_F(MetricsIntegrationTest, SurveillanceAlertsTotal) {
    Metrics::instance().increment("surveillance.alerts_total");
    EXPECT_EQ(Metrics::instance().get_counter("surveillance.alerts_total"), 1u);

    auto prom = Metrics::instance().to_prometheus();
    EXPECT_NE(prom.find("surveillance_alerts_total"), std::string::npos);
}

// ─── All new metric keys appear in Prometheus output ────────────

TEST_F(MetricsIntegrationTest, AllNewMetricKeysInPrometheus) {
    // Exercise all new metric keys
    Metrics::instance().increment("engine.matches.total");
    Metrics::instance().set_gauge("engine.orders.resting", 100);
    Metrics::instance().increment("market_data.messages_published");
    Metrics::instance().increment("surveillance.alerts_total");
    Metrics::instance().set_gauge("fix.active_sessions", 3);
    Metrics::instance().increment("journal.entries_written");

    auto prom = Metrics::instance().to_prometheus();

    // Counters
    EXPECT_NE(prom.find("# TYPE engine_matches_total counter"), std::string::npos);
    EXPECT_NE(prom.find("# TYPE market_data_messages_published counter"), std::string::npos);
    EXPECT_NE(prom.find("# TYPE surveillance_alerts_total counter"), std::string::npos);
    EXPECT_NE(prom.find("# TYPE journal_entries_written counter"), std::string::npos);

    // Gauges
    EXPECT_NE(prom.find("# TYPE engine_orders_resting gauge"), std::string::npos);
    EXPECT_NE(prom.find("# TYPE fix_active_sessions gauge"), std::string::npos);
}

// ─── JSON output includes all new keys ──────────────────────────

TEST_F(MetricsIntegrationTest, AllNewMetricKeysInJson) {
    Metrics::instance().increment("engine.matches.total", 7);
    Metrics::instance().set_gauge("engine.orders.resting", 50);
    Metrics::instance().increment("market_data.messages_published", 15);
    Metrics::instance().set_gauge("fix.active_sessions", 2);
    Metrics::instance().increment("journal.entries_written", 20);

    auto json = Metrics::instance().to_json();

    EXPECT_NE(json.find("\"engine.matches.total\":7"), std::string::npos);
    EXPECT_NE(json.find("\"market_data.messages_published\":15"), std::string::npos);
    EXPECT_NE(json.find("\"journal.entries_written\":20"), std::string::npos);
    EXPECT_NE(json.find("\"engine.orders.resting\":50"), std::string::npos);
    EXPECT_NE(json.find("\"fix.active_sessions\":2"), std::string::npos);
}

}  // namespace
}  // namespace ops
