/// Full platform stress test — multi-instrument, multi-participant.
///
/// NOTE: This is a prototype stress validation, NOT a production capacity test.
/// Results are directional indicators only, not exchange-grade benchmarks.

#include "engine/partition.h"
#include "engine/journal_stub.h"
#include "engine/session_lifecycle.h"
#include "engine/gtd_expiry_checker.h"
#include "engine/session_scheduler.h"
#include "marketdata/publisher.h"
#include "dropcopy/drop_copy_service.h"

#include <gtest/gtest.h>
#include <chrono>
#include <random>
#include <iostream>

namespace engine {
namespace {

constexpr uint32_t NUM_INSTRUMENTS = 5;
constexpr uint32_t NUM_PARTICIPANTS = 3;
constexpr uint32_t ORDERS_PER_INSTRUMENT = 2000;
constexpr uint32_t TOTAL_ORDERS = NUM_INSTRUMENTS * ORDERS_PER_INSTRUMENT;

InstrumentConfig make_config(InstrumentId id) {
    InstrumentConfig cfg{};
    cfg.instrument_id = id;
    cfg.tick_size = 1; cfg.lot_size = 1;
    cfg.min_quantity = 1; cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000; cfg.static_band_pct = 30.0;
    cfg.dynamic_band_pct = 15.0;
    cfg.residual_policy = ResidualPolicy::Cancel;
    cfg.collar_ticks = 10;
    cfg.stp_mode = STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    return cfg;
}

PartitionMessage make_order(InstrumentId inst, Side side, Price price,
                             Quantity qty, ParticipantId part_id,
                             ClientOrderId clord, TimeInForce tif = TimeInForce::Day) {
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::NewOrder;
    msg.timestamp = 1000; msg.client_order_id = clord;
    msg.instrument_id = inst; msg.side = side;
    msg.order_type = OrderType::Limit; msg.time_in_force = tif;
    msg.price = price; msg.quantity = qty;
    msg.participant_id = part_id; msg.account_id = 1; msg.session_id = 1;
    return msg;
}

TEST(StressTest, MultiInstrumentMultiParticipantLoad) {
    JournalStub journal(TOTAL_ORDERS * 4);
    Partition partition(1, TOTAL_ORDERS * 2, &journal);

    // Register instruments
    for (uint32_t i = 0; i < NUM_INSTRUMENTS; ++i) {
        partition.add_instrument(make_config(100 + i));
    }

    // Connect market data and drop copy
    marketdata::MarketDataPublisher md_pub;
    marketdata::TestPublisherSink md_sink;
    md_pub.add_sink(&md_sink);

    dropcopy::DropCopyService dc_svc;
    dropcopy::TestDropCopySink dc_sinks[NUM_PARTICIPANTS];
    for (uint32_t p = 0; p < NUM_PARTICIPANTS; ++p) {
        dc_svc.register_sink(p + 1, &dc_sinks[p]);
    }

    // Counters
    uint32_t inserted = 0, filled = 0, cancelled = 0, rejected = 0;
    ClientOrderId clord = 1;

    std::mt19937 rng(42);  // Fixed seed for reproducibility

    auto start_time = std::chrono::high_resolution_clock::now();

    // Phase 1: Insert resting orders (alternating buys and sells at spread)
    for (uint32_t i = 0; i < NUM_INSTRUMENTS; ++i) {
        InstrumentId inst = 100 + i;
        for (uint32_t j = 0; j < ORDERS_PER_INSTRUMENT / 2; ++j) {
            ParticipantId part = (j % NUM_PARTICIPANTS) + 1;
            Price buy_price = 9000 + (rng() % 500);   // 9000–9499
            Price sell_price = 10500 + (rng() % 500);  // 10500–10999
            Quantity qty = 1 + (rng() % 50);

            auto r1 = partition.process(make_order(inst, Side::Buy, buy_price, qty, part, clord++));
            if (r1.action == MatchResult::Action::Inserted) ++inserted;
            else if (r1.action == MatchResult::Action::Filled) ++filled;
            else if (r1.action == MatchResult::Action::Rejected) ++rejected;

            auto r2 = partition.process(make_order(inst, Side::Sell, sell_price, qty, part, clord++));
            if (r2.action == MatchResult::Action::Inserted) ++inserted;
            else if (r2.action == MatchResult::Action::Filled) ++filled;
            else if (r2.action == MatchResult::Action::Rejected) ++rejected;
        }
    }

    // Phase 2: Aggressive crossing orders (should produce trades)
    [[maybe_unused]] uint32_t crossing_orders = 0;
    for (uint32_t i = 0; i < NUM_INSTRUMENTS; ++i) {
        InstrumentId inst = 100 + i;
        for (uint32_t j = 0; j < 100; ++j) {
            ParticipantId part = (j % NUM_PARTICIPANTS) + 1;
            // Buy at high price to sweep asks
            auto result = partition.process(
                make_order(inst, Side::Buy, 11000, 5, part, clord++));
            ++crossing_orders;
            if (result.action == MatchResult::Action::Filled) ++filled;
            else if (result.action == MatchResult::Action::Inserted) ++inserted;
        }
    }

    // Phase 3: Cancel some resting orders
    uint32_t cancel_attempts = 0;
    for (uint32_t i = 0; i < NUM_INSTRUMENTS; ++i) {
        const auto* eng = partition.get_engine(100 + i);
        if (!eng) continue;

        std::vector<OrderId> to_cancel;
        eng->book().for_each_order([&](const Order& order) {
            if (to_cancel.size() < 50) to_cancel.push_back(order.order_id);
        });

        for (OrderId oid : to_cancel) {
            PartitionMessage cancel{};
            cancel.type = PartitionMessage::Type::Cancel;
            cancel.timestamp = 5000;
            cancel.orig_order_id = oid;
            auto result = partition.process(cancel);
            ++cancel_attempts;
            if (result.action == MatchResult::Action::Cancelled) ++cancelled;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    uint32_t total_messages = clord - 1 + cancel_attempts;

    // Report (informational — not assertions on timing)
    std::cout << "\n=== STRESS TEST RESULTS (prototype — not exchange-grade) ===" << "\n";
    std::cout << "Instruments: " << NUM_INSTRUMENTS << "\n";
    std::cout << "Participants: " << NUM_PARTICIPANTS << "\n";
    std::cout << "Total messages: " << total_messages << "\n";
    std::cout << "  Inserted: " << inserted << "\n";
    std::cout << "  Filled: " << filled << "\n";
    std::cout << "  Cancelled: " << cancelled << "\n";
    std::cout << "  Rejected: " << rejected << "\n";
    std::cout << "Elapsed: " << elapsed_ms << " ms\n";
    if (elapsed_ms > 0) {
        std::cout << "Throughput: " << (total_messages * 1000 / elapsed_ms) << " msg/s\n";
    }
    std::cout << "Journal entries: " << journal.entry_count() << "\n";
    std::cout << "Partition sequence: " << partition.sequence() << "\n";
    std::cout << "=== END STRESS TEST ===" << "\n\n";

    // ── INVARIANT CHECKS ────────────────────────────────────

    // All messages journaled (2 entries per message: input + output)
    EXPECT_EQ(journal.entry_count(), partition.sequence() * 2);

    // Partition sequence matches total processed messages
    EXPECT_EQ(partition.sequence(), static_cast<SequenceNum>(total_messages));

    // All instruments still have valid books
    for (uint32_t i = 0; i < NUM_INSTRUMENTS; ++i) {
        const auto* eng = partition.get_engine(100 + i);
        ASSERT_NE(eng, nullptr);
        // Book should have some resting orders (not all consumed)
        // Just check it doesn't crash
        (void)eng->book().best_bid();
        (void)eng->book().best_ask();
        (void)eng->book().order_count();
    }

    // Reasonable operation counts
    EXPECT_GT(inserted, 0u);
    EXPECT_GT(filled, 0u);
    EXPECT_GT(cancelled, 0u);
    EXPECT_EQ(rejected, 0u);  // All orders should be valid in this scenario
}

TEST(StressTest, SessionLifecycleUnderLoad) {
    JournalStub journal(50000);
    Partition partition(1, 20000, &journal);
    partition.add_instrument(make_config(100));

    // Insert mix of Day and GTC orders
    for (uint32_t i = 0; i < 500; ++i) {
        TimeInForce tif = (i % 3 == 0) ? TimeInForce::GTC : TimeInForce::Day;
        Price price = 9000 + (i % 1000);
        (void)partition.process(make_order(100, Side::Buy, price, 10, 1, i + 1, tif));
    }

    uint32_t initial_count = partition.get_engine(100)->book().order_count();
    EXPECT_EQ(initial_count, 500u);

    // Close session — Day orders cancelled, GTC remain
    uint32_t cancelled = SessionLifecycle::on_session_close(partition, 100, 10000);
    uint32_t remaining = partition.get_engine(100)->book().order_count();

    // ~167 GTC orders should remain (every 3rd order)
    EXPECT_GT(remaining, 100u);
    EXPECT_LT(remaining, 200u);
    EXPECT_EQ(cancelled + remaining, initial_count);
}

}  // namespace
}  // namespace engine
