#include "engine/pipeline_processor.h"
#include "engine/spsc_ring_buffer.h"
#include "risk/risk_service.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace engine;

// ═══════════════════════════════════════════════════════════════
// SPSC Ring Buffer Tests
// ═══════════════════════════════════════════════════════════════

TEST(SPSCRingBuffer, PublishConsume) {
    SPSCRingBuffer<uint64_t, 1024> ring;
    constexpr int COUNT = 1000;

    for (int i = 0; i < COUNT; ++i) {
        ASSERT_TRUE(ring.try_publish(static_cast<uint64_t>(i)));
    }

    for (int i = 0; i < COUNT; ++i) {
        uint64_t val = 0;
        ASSERT_TRUE(ring.try_consume(val));
        EXPECT_EQ(val, static_cast<uint64_t>(i));
    }

    // Buffer should be empty now.
    uint64_t dummy = 0;
    EXPECT_FALSE(ring.try_consume(dummy));
}

TEST(SPSCRingBuffer, FullBufferRejectsFurther) {
    SPSCRingBuffer<int, 16> ring;

    // Fill to capacity.
    for (int i = 0; i < 16; ++i) {
        ASSERT_TRUE(ring.try_publish(i));
    }

    // 17th publish should fail.
    EXPECT_FALSE(ring.try_publish(999));
    EXPECT_EQ(ring.available(), 16u);

    // Consume one, then publish should succeed again.
    int val = 0;
    ASSERT_TRUE(ring.try_consume(val));
    EXPECT_EQ(val, 0);
    EXPECT_TRUE(ring.try_publish(999));
}

TEST(SPSCRingBuffer, EmptyBufferReturnsEmpty) {
    SPSCRingBuffer<int, 64> ring;

    int val = 42;
    EXPECT_FALSE(ring.try_consume(val));
    // val should be unchanged (consume failed).
    EXPECT_EQ(val, 42);
    EXPECT_FALSE(ring.has_available());
    EXPECT_EQ(ring.available(), 0u);
}

TEST(SPSCRingBuffer, ConcurrentProducerConsumer) {
    SPSCRingBuffer<uint64_t, 4096> ring;
    constexpr uint64_t COUNT = 100000;

    std::atomic<bool> done{false};
    std::vector<uint64_t> consumed;
    consumed.reserve(COUNT);

    // Consumer thread.
    std::thread consumer([&] {
        uint64_t expected = 0;
        while (expected < COUNT) {
            uint64_t val = 0;
            if (ring.try_consume(val)) {
                consumed.push_back(val);
                ++expected;
            } else {
                std::this_thread::yield();
            }
        }
        done.store(true, std::memory_order_release);
    });

    // Producer thread.
    std::thread producer([&] {
        for (uint64_t i = 0; i < COUNT; ++i) {
            while (!ring.try_publish(i)) {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    // Verify all received in order.
    ASSERT_EQ(consumed.size(), COUNT);
    for (uint64_t i = 0; i < COUNT; ++i) {
        EXPECT_EQ(consumed[i], i) << "Out of order at index " << i;
    }
}

// ═══════════════════════════════════════════════════════════════
// Pipeline Processor Tests
// ═══════════════════════════════════════════════════════════════

namespace {

/// Helper to create a default InstrumentConfig for tests.
InstrumentConfig make_test_instrument(InstrumentId id) {
    InstrumentConfig cfg{};
    cfg.instrument_id = id;
    cfg.tick_size = 1;
    cfg.lot_size = 1;
    cfg.min_quantity = 1;
    cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000;
    cfg.static_band_pct = 50.0;
    cfg.dynamic_band_pct = 50.0;
    cfg.residual_policy = ResidualPolicy::Cancel;
    cfg.collar_ticks = 10;
    cfg.stp_mode = STPMode::CancelNewest;
    return cfg;
}

/// Helper to create a basic NewOrder PartitionMessage.
PartitionMessage make_test_order(InstrumentId inst_id,
                                  Side side = Side::Buy,
                                  Price price = 10000,
                                  Quantity qty = 100) {
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::NewOrder;
    msg.sequence = 0;
    msg.timestamp = 1000;
    msg.client_order_id = 1;
    msg.instrument_id = inst_id;
    msg.side = side;
    msg.order_type = OrderType::Limit;
    msg.time_in_force = TimeInForce::Day;
    msg.price = price;
    msg.quantity = qty;
    msg.participant_id = 1;
    msg.account_id = 1;
    msg.session_id = 1;
    msg.stp_group_id = 0;
    msg.flags = 0;
    msg.expiry_time = 0;
    return msg;
}

/// A risk service that always passes.
class AlwaysPassRiskService : public risk::IRiskService {
public:
    risk::RiskResponse check(const risk::RiskRequest& /*req*/) override {
        return {risk::RiskResult::PASS, RejectCode::None};
    }
    void update_limits(ParticipantId, const risk::RiskLimits&) override {}
    void block_participant(ParticipantId) override {}
    void unblock_participant(ParticipantId) override {}
    bool is_blocked(ParticipantId) const override { return false; }
};

/// A risk service that always rejects.
class AlwaysRejectRiskService : public risk::IRiskService {
public:
    risk::RiskResponse check(const risk::RiskRequest& /*req*/) override {
        return {risk::RiskResult::REJECT, RejectCode::RiskCheckFailed};
    }
    void update_limits(ParticipantId, const risk::RiskLimits&) override {}
    void block_participant(ParticipantId) override {}
    void unblock_participant(ParticipantId) override {}
    bool is_blocked(ParticipantId) const override { return false; }
};

}  // anonymous namespace

TEST(Pipeline, StartStop) {
    AlwaysPassRiskService risk;
    OrderPool pool(1024);
    auto cfg = make_test_instrument(1);
    std::unordered_map<InstrumentId, MatchingEngine> engines;
    engines.try_emplace(1, cfg, pool);

    PipelineProcessor::Config config{};
    PipelineProcessor pipeline(config, &risk, engines);

    EXPECT_FALSE(pipeline.is_running());

    pipeline.start();
    EXPECT_TRUE(pipeline.is_running());

    // Let threads spin briefly.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    pipeline.stop();
    EXPECT_FALSE(pipeline.is_running());
}

TEST(Pipeline, ProcessOrder) {
    AlwaysPassRiskService risk;
    OrderPool pool(1024);
    auto cfg = make_test_instrument(1);
    std::unordered_map<InstrumentId, MatchingEngine> engines;
    engines.try_emplace(1, cfg, pool);

    PipelineProcessor::Config config{};
    PipelineProcessor pipeline(config, &risk, engines);

    std::atomic<int> output_count{0};
    PipelineEvent last_event{};

    pipeline.set_output_callback([&](const PipelineEvent& evt) {
        last_event = evt;
        output_count.fetch_add(1, std::memory_order_relaxed);
    });

    pipeline.start();

    auto msg = make_test_order(1);
    ASSERT_TRUE(pipeline.submit(msg));

    // Wait for the event to flow through the pipeline.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (output_count.load(std::memory_order_relaxed) < 1 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    pipeline.stop();

    EXPECT_EQ(output_count.load(), 1);
    EXPECT_TRUE(last_event.risk_passed);
    EXPECT_EQ(last_event.instrument_id, 1u);
    // Order passed risk and was processed by match stage.
    EXPECT_NE(last_event.match_result.action, MatchResult::Action::Rejected);
}

TEST(Pipeline, RiskReject) {
    AlwaysRejectRiskService risk;
    OrderPool pool(1024);
    auto cfg = make_test_instrument(1);
    std::unordered_map<InstrumentId, MatchingEngine> engines;
    engines.try_emplace(1, cfg, pool);

    PipelineProcessor::Config config{};
    PipelineProcessor pipeline(config, &risk, engines);

    std::atomic<int> output_count{0};
    PipelineEvent last_event{};

    pipeline.set_output_callback([&](const PipelineEvent& evt) {
        last_event = evt;
        output_count.fetch_add(1, std::memory_order_relaxed);
    });

    pipeline.start();

    auto msg = make_test_order(1);
    ASSERT_TRUE(pipeline.submit(msg));

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (output_count.load(std::memory_order_relaxed) < 1 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    pipeline.stop();

    EXPECT_EQ(output_count.load(), 1);
    EXPECT_FALSE(last_event.risk_passed);
    EXPECT_EQ(last_event.risk_reject, RejectCode::RiskCheckFailed);
    EXPECT_EQ(last_event.match_result.action, MatchResult::Action::Rejected);
    EXPECT_EQ(pipeline.risk_rejects(), 1u);
}

TEST(Pipeline, HighThroughput) {
    AlwaysPassRiskService risk;
    OrderPool pool(1024);
    auto cfg = make_test_instrument(1);
    std::unordered_map<InstrumentId, MatchingEngine> engines;
    engines.try_emplace(1, cfg, pool);

    PipelineProcessor::Config config{};
    PipelineProcessor pipeline(config, &risk, engines);

    constexpr int COUNT = 100000;
    std::atomic<int> output_count{0};

    pipeline.set_output_callback([&](const PipelineEvent& /*evt*/) {
        output_count.fetch_add(1, std::memory_order_relaxed);
    });

    pipeline.start();

    // Submit all orders from the gateway thread.
    for (int i = 0; i < COUNT; ++i) {
        auto msg = make_test_order(1, Side::Buy, 10000, 100);
        msg.client_order_id = static_cast<uint64_t>(i);
        while (!pipeline.submit(msg)) {
            // Backpressure — spin until space available.
            std::this_thread::yield();
        }
    }

    // Wait for all events to flow through.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (output_count.load(std::memory_order_relaxed) < COUNT &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pipeline.stop();

    EXPECT_EQ(output_count.load(), COUNT);
    EXPECT_EQ(pipeline.events_processed(), static_cast<uint64_t>(COUNT));
}

TEST(Pipeline, Metrics) {
    AlwaysPassRiskService risk;
    OrderPool pool(1024);
    auto cfg = make_test_instrument(1);
    std::unordered_map<InstrumentId, MatchingEngine> engines;
    engines.try_emplace(1, cfg, pool);

    PipelineProcessor::Config config{};
    PipelineProcessor pipeline(config, &risk, engines);

    // Before start: metrics should be zero.
    EXPECT_EQ(pipeline.events_processed(), 0u);
    EXPECT_EQ(pipeline.risk_rejects(), 0u);
    EXPECT_EQ(pipeline.ring_buffer_usage(), 0u);

    std::atomic<int> output_count{0};
    pipeline.set_output_callback([&](const PipelineEvent& /*evt*/) {
        output_count.fetch_add(1, std::memory_order_relaxed);
    });

    pipeline.start();

    constexpr int COUNT = 50;
    for (int i = 0; i < COUNT; ++i) {
        auto msg = make_test_order(1);
        msg.client_order_id = static_cast<uint64_t>(i);
        ASSERT_TRUE(pipeline.submit(msg));
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (output_count.load(std::memory_order_relaxed) < COUNT &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    pipeline.stop();

    EXPECT_EQ(pipeline.events_processed(), static_cast<uint64_t>(COUNT));
    EXPECT_EQ(pipeline.risk_rejects(), 0u);
}
