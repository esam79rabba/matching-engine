#include "ops/metrics.h"
#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace ops {
namespace {

class MetricsTest : public ::testing::Test {
protected:
    void SetUp() override {
        Metrics::instance().reset();
    }
};

TEST_F(MetricsTest, IncrementCounter) {
    Metrics::instance().increment("test.counter");
    EXPECT_EQ(Metrics::instance().get_counter("test.counter"), 1u);

    Metrics::instance().increment("test.counter");
    EXPECT_EQ(Metrics::instance().get_counter("test.counter"), 2u);

    Metrics::instance().increment("test.counter", 5);
    EXPECT_EQ(Metrics::instance().get_counter("test.counter"), 7u);
}

TEST_F(MetricsTest, SetGauge) {
    Metrics::instance().set_gauge("test.gauge", 42);
    EXPECT_EQ(Metrics::instance().get_gauge("test.gauge"), 42);

    Metrics::instance().set_gauge("test.gauge", -10);
    EXPECT_EQ(Metrics::instance().get_gauge("test.gauge"), -10);

    Metrics::instance().set_gauge("test.gauge", 0);
    EXPECT_EQ(Metrics::instance().get_gauge("test.gauge"), 0);
}

TEST_F(MetricsTest, PrometheusFormat) {
    Metrics::instance().increment("fix.orders_accepted", 42);
    Metrics::instance().set_gauge("partition.instrument_count", 5);

    auto output = Metrics::instance().to_prometheus();

    // Dots replaced with underscores
    EXPECT_NE(output.find("# TYPE fix_orders_accepted counter"), std::string::npos);
    EXPECT_NE(output.find("fix_orders_accepted 42"), std::string::npos);
    EXPECT_NE(output.find("# TYPE partition_instrument_count gauge"), std::string::npos);
    EXPECT_NE(output.find("partition_instrument_count 5"), std::string::npos);

    // Dots should not appear as metric names
    EXPECT_EQ(output.find("fix.orders_accepted"), std::string::npos);
}

TEST_F(MetricsTest, JsonFormat) {
    Metrics::instance().increment("fix.orders", 10);
    Metrics::instance().set_gauge("active.sessions", 3);

    auto json = Metrics::instance().to_json();

    EXPECT_NE(json.find("\"counters\""), std::string::npos);
    EXPECT_NE(json.find("\"gauges\""), std::string::npos);
    EXPECT_NE(json.find("\"fix.orders\":10"), std::string::npos);
    EXPECT_NE(json.find("\"active.sessions\":3"), std::string::npos);
}

TEST_F(MetricsTest, ResetClearsAll) {
    Metrics::instance().increment("counter1", 5);
    Metrics::instance().set_gauge("gauge1", 42);

    Metrics::instance().reset();

    EXPECT_EQ(Metrics::instance().get_counter("counter1"), 0u);
    EXPECT_EQ(Metrics::instance().get_gauge("gauge1"), 0);

    auto prom = Metrics::instance().to_prometheus();
    EXPECT_TRUE(prom.empty());
}

TEST_F(MetricsTest, UnknownCounterReturnsZero) {
    EXPECT_EQ(Metrics::instance().get_counter("nonexistent.counter"), 0u);
    EXPECT_EQ(Metrics::instance().get_gauge("nonexistent.gauge"), 0);
}

TEST_F(MetricsTest, ThreadSafety) {
    constexpr int num_threads = 8;
    constexpr int increments_per_thread = 10000;

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < increments_per_thread; ++i) {
                Metrics::instance().increment("concurrent.counter");
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    EXPECT_EQ(Metrics::instance().get_counter("concurrent.counter"),
              static_cast<uint64_t>(num_threads * increments_per_thread));
}

}  // namespace
}  // namespace ops
