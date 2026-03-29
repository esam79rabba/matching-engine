/// Production-grade matching engine benchmark suite.
///
/// Measures latency at sustained load across five scenarios:
///   1. Single instrument (exchange-core comparison)
///   2. Multi-instrument (100 instruments)
///   3. Sustained throughput (1M ops/sec target, 10s)
///   4. Burst (5M ops as fast as possible)
///   5. With risk checks (BasicRiskService enabled)
///
/// Uses HdrHistogram-style percentile tracking (inline, no external deps).
/// Output format is human-readable with structured JSON for CI consumption.

#include "latency_histogram.h"

#include "engine/partition.h"
#include "engine/journal_stub.h"
#include "engine/matching.h"
#include "risk/risk_service.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

using namespace engine;
using Clock = std::chrono::high_resolution_clock;
using Nanos = std::chrono::nanoseconds;

// ─── Helpers ────────────────────────────────────────────────────────────────

static std::string format_number(int64_t n) {
    std::string s = std::to_string(n);
    int insert_pos = static_cast<int>(s.length()) - 3;
    while (insert_pos > 0) {
        s.insert(insert_pos, ",");
        insert_pos -= 3;
    }
    return s;
}

static void pin_to_core([[maybe_unused]] int core_id) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
    // macOS: no portable CPU pinning; skip silently.
}

static void print_separator() {
    std::cout << "\n" << std::string(72, '=') << "\n";
}

static void print_histogram_line(const char* label, benchmark::LatencyHistogram& h) {
    std::cout << "    " << std::left << std::setw(12) << label
              << "p50=" << std::setw(9) << benchmark::LatencyHistogram::format_ns(h.percentile(50.0))
              << "  p99=" << std::setw(9) << benchmark::LatencyHistogram::format_ns(h.percentile(99.0))
              << "  p99.9=" << benchmark::LatencyHistogram::format_ns(h.percentile(99.9))
              << "\n";
}

// ─── Instrument configuration ───────────────────────────────────────────────

static InstrumentConfig make_config(InstrumentId id, Price ref_price = 100000) {
    InstrumentConfig cfg{};
    cfg.instrument_id = id;
    cfg.tick_size = 1;
    cfg.lot_size = 1;
    cfg.min_quantity = 1;
    cfg.max_quantity = 100000000;
    cfg.static_ref_price = ref_price;
    cfg.static_band_pct = 50.0;
    cfg.dynamic_band_pct = 50.0;
    cfg.residual_policy = ResidualPolicy::Cancel;
    cfg.collar_ticks = 100;
    cfg.stp_mode = STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    return cfg;
}

// ─── Message generators ─────────────────────────────────────────────────────

static PartitionMessage make_new_order(InstrumentId inst, Side side, OrderType ot,
                                       Price price, Quantity qty, ClientOrderId clord) {
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::NewOrder;
    msg.timestamp = 0;
    msg.client_order_id = clord;
    msg.instrument_id = inst;
    msg.side = side;
    msg.order_type = ot;
    msg.time_in_force = (ot == OrderType::Market) ? TimeInForce::IOC : TimeInForce::Day;
    msg.price = price;
    msg.quantity = qty;
    msg.participant_id = 1;
    msg.account_id = 1;
    msg.session_id = 1;
    return msg;
}

static PartitionMessage make_cancel(InstrumentId inst, OrderId orig_id) {
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::Cancel;
    msg.timestamp = 0;
    msg.instrument_id = inst;
    msg.orig_order_id = orig_id;
    return msg;
}

static PartitionMessage make_modify(InstrumentId inst, OrderId orig_id,
                                    Price new_price, Quantity new_qty) {
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::Modify;
    msg.timestamp = 0;
    msg.instrument_id = inst;
    msg.orig_order_id = orig_id;
    msg.new_price = new_price;
    msg.new_qty = new_qty;
    return msg;
}

// ─── Pre-populate a book with resting orders ────────────────────────────────

/// Populate a partition's instrument with resting orders spread around ref_price.
/// Returns the next available client order ID.
static ClientOrderId populate_book(Partition& part, InstrumentId inst_id,
                                   Price ref_price, int num_orders, int num_levels,
                                   ClientOrderId start_clord) {
    std::mt19937 rng(42);

    int orders_per_level = std::max(1, num_orders / (num_levels * 2));

    ClientOrderId clord = start_clord;

    for (int level = 0; level < num_levels; ++level) {
        Price bid_price = ref_price - 1 - level;
        Price ask_price = ref_price + 1 + level;

        for (int j = 0; j < orders_per_level; ++j) {
            Quantity qty = 10 + (rng() % 90);
            part.process(make_new_order(inst_id, Side::Buy, OrderType::Limit,
                                        bid_price, qty, clord++));
            part.process(make_new_order(inst_id, Side::Sell, OrderType::Limit,
                                        ask_price, qty, clord++));
        }
    }
    return clord;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Scenario 1: Single Instrument — Exchange-Core Comparison
// ═══════════════════════════════════════════════════════════════════════════

static void scenario_1_single_instrument() {
    constexpr InstrumentId INST   = 100;
    constexpr PartitionId  PART   = 1;
    constexpr uint32_t POOL_SIZE  = 2000000;
    constexpr int WARMUP          = 10000;
    constexpr int OPS             = 3000000;
    constexpr Price REF_PRICE     = 100000;

    print_separator();
    std::cout << "  Matching Engine Benchmark -- Scenario 1 (Single Instrument)\n"
              << std::string(72, '=') << "\n";

    JournalStub journal(POOL_SIZE * 2);
    Partition part(PART, POOL_SIZE, &journal);
    part.add_instrument(make_config(INST, REF_PRICE));

    // Pre-populate: ~1000 active orders across 375 price levels per side (750 total)
    ClientOrderId next_clord = populate_book(part, INST, REF_PRICE, 1000, 375, 1);

    // Operation mix: 60% new limit, 20% cancel, 15% modify, 5% market
    std::mt19937 rng(12345);
    std::normal_distribution<double> price_dist(0.0, 50.0);
    std::uniform_int_distribution<int> op_dist(0, 99);
    std::uniform_int_distribution<int> qty_dist(1, 100);

    // Collect order IDs for cancel/modify operations
    std::vector<OrderId> live_orders;
    live_orders.reserve(100000);

    benchmark::LatencyHistogram overall;
    benchmark::LatencyHistogram insert_hist;
    benchmark::LatencyHistogram match_hist;
    benchmark::LatencyHistogram cancel_hist;
    benchmark::LatencyHistogram modify_hist;

    auto wall_start = Clock::now();

    for (int i = 0; i < WARMUP + OPS; ++i) {
        int op = op_dist(rng);
        int64_t latency_ns = 0;

        if (op < 60) {
            // New limit order (non-crossing to rest in book)
            Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
            Price offset = static_cast<Price>(std::abs(price_dist(rng)));
            Price price = (side == Side::Buy)
                ? REF_PRICE - 5 - offset
                : REF_PRICE + 5 + offset;
            Quantity qty = qty_dist(rng);

            auto msg = make_new_order(INST, side, OrderType::Limit, price, qty, next_clord++);
            auto t0 = Clock::now();
            auto result = part.process(msg);
            auto t1 = Clock::now();
            latency_ns = std::chrono::duration_cast<Nanos>(t1 - t0).count();

            if (i >= WARMUP) insert_hist.record(latency_ns);

            if (result.assigned_order_id != 0 && result.order_resting) {
                live_orders.push_back(result.assigned_order_id);
            }
        } else if (op < 65) {
            // Market order (aggressive — will match)
            Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
            Price price = (side == Side::Buy) ? REF_PRICE + 1000 : REF_PRICE - 1000;
            Quantity qty = 1 + (rng() % 5);

            auto msg = make_new_order(INST, side, OrderType::Limit, price, qty, next_clord++);
            auto t0 = Clock::now();
            part.process(msg);
            auto t1 = Clock::now();
            latency_ns = std::chrono::duration_cast<Nanos>(t1 - t0).count();

            if (i >= WARMUP) match_hist.record(latency_ns);
        } else if (op < 80 && !live_orders.empty()) {
            // Cancel
            size_t idx = rng() % live_orders.size();
            OrderId oid = live_orders[idx];
            // Swap-remove for O(1)
            live_orders[idx] = live_orders.back();
            live_orders.pop_back();

            auto msg = make_cancel(INST, oid);
            auto t0 = Clock::now();
            part.process(msg);
            auto t1 = Clock::now();
            latency_ns = std::chrono::duration_cast<Nanos>(t1 - t0).count();

            if (i >= WARMUP) cancel_hist.record(latency_ns);
        } else if (!live_orders.empty()) {
            // Modify
            size_t idx = rng() % live_orders.size();
            OrderId oid = live_orders[idx];

            Price new_price = REF_PRICE + static_cast<Price>(price_dist(rng));
            Quantity new_qty = qty_dist(rng);

            auto msg = make_modify(INST, oid, new_price, new_qty);
            auto t0 = Clock::now();
            part.process(msg);
            auto t1 = Clock::now();
            latency_ns = std::chrono::duration_cast<Nanos>(t1 - t0).count();

            if (i >= WARMUP) modify_hist.record(latency_ns);
        } else {
            // Fallback: insert if no live orders for cancel/modify
            Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
            Price price = (side == Side::Buy) ? REF_PRICE - 10 : REF_PRICE + 10;
            auto msg = make_new_order(INST, side, OrderType::Limit, price, 10, next_clord++);
            auto t0 = Clock::now();
            auto result = part.process(msg);
            auto t1 = Clock::now();
            latency_ns = std::chrono::duration_cast<Nanos>(t1 - t0).count();

            if (i >= WARMUP) insert_hist.record(latency_ns);
            if (result.assigned_order_id != 0 && result.order_resting) {
                live_orders.push_back(result.assigned_order_id);
            }
        }

        if (i >= WARMUP) overall.record(latency_ns);
    }

    auto wall_end = Clock::now();
    double wall_s = std::chrono::duration_cast<Nanos>(wall_end - wall_start).count() / 1e9;
    double throughput = static_cast<double>(OPS) / wall_s;

    std::cout << "  Operations:    " << format_number(OPS) << "\n"
              << "  Duration:      " << std::fixed << std::setprecision(2) << wall_s << " seconds\n"
              << "  Throughput:    " << format_number(static_cast<int64_t>(throughput)) << " ops/sec\n"
              << "\n"
              << overall.summary()
              << "\n  Breakdown:\n";
    print_histogram_line("Insert:", insert_hist);
    print_histogram_line("Match:", match_hist);
    print_histogram_line("Cancel:", cancel_hist);
    print_histogram_line("Modify:", modify_hist);
    std::cout << std::string(72, '=') << "\n";

    // JSON for CI
    std::cout << "\n=== JSON:scenario1 ===\n"
              << std::fixed << std::setprecision(1)
              << "{\"ops\":" << OPS
              << ",\"throughput\":" << std::setprecision(0) << throughput
              << ",\"p50_ns\":" << overall.percentile(50.0)
              << ",\"p99_ns\":" << overall.percentile(99.0)
              << ",\"p999_ns\":" << overall.percentile(99.9)
              << ",\"p9999_ns\":" << overall.percentile(99.99)
              << ",\"worst_ns\":" << overall.max()
              << ",\"mean_ns\":" << std::setprecision(1) << overall.mean()
              << ",\"insert_p99_ns\":" << insert_hist.percentile(99.0)
              << ",\"match_p99_ns\":" << match_hist.percentile(99.0)
              << ",\"cancel_p99_ns\":" << cancel_hist.percentile(99.0)
              << "}\n";
}

// ═══════════════════════════════════════════════════════════════════════════
//  Scenario 2: Multi-Instrument
// ═══════════════════════════════════════════════════════════════════════════

static void scenario_2_multi_instrument() {
    constexpr PartitionId PART   = 1;
    constexpr uint32_t POOL_SIZE = 5000000;
    constexpr int NUM_INST       = 100;
    constexpr int ORDERS_PER     = 10000;
    constexpr int OPS            = 1000000;
    constexpr int WARMUP         = 10000;
    constexpr Price REF_PRICE    = 100000;

    print_separator();
    std::cout << "  Matching Engine Benchmark -- Scenario 2 (Multi-Instrument)\n"
              << std::string(72, '=') << "\n";
    std::cout << "  Instruments:   " << NUM_INST << "\n"
              << "  Orders/inst:   " << format_number(ORDERS_PER) << "\n\n";

    JournalStub journal(POOL_SIZE * 2);
    Partition part(PART, POOL_SIZE, &journal);

    // Add instruments and populate
    ClientOrderId next_clord = 1;
    std::vector<InstrumentId> inst_ids;
    for (int i = 0; i < NUM_INST; ++i) {
        InstrumentId id = 100 + i;
        inst_ids.push_back(id);
        part.add_instrument(make_config(id, REF_PRICE));
        next_clord = populate_book(part, id, REF_PRICE, ORDERS_PER / 10, 50,
                                   next_clord);
    }

    std::mt19937 rng(67890);
    std::uniform_int_distribution<int> inst_dist(0, NUM_INST - 1);
    std::normal_distribution<double> price_dist(0.0, 30.0);
    std::uniform_int_distribution<int> qty_dist(1, 100);

    benchmark::LatencyHistogram overall;

    auto wall_start = Clock::now();

    for (int i = 0; i < WARMUP + OPS; ++i) {
        InstrumentId inst = inst_ids[inst_dist(rng)];

        Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
        Price offset = static_cast<Price>(std::abs(price_dist(rng)));
        Price price = (side == Side::Buy) ? REF_PRICE - 5 - offset : REF_PRICE + 5 + offset;
        Quantity qty = qty_dist(rng);

        auto msg = make_new_order(inst, side, OrderType::Limit, price, qty, next_clord++);

        auto t0 = Clock::now();
        part.process(msg);
        auto t1 = Clock::now();

        if (i >= WARMUP) {
            overall.record(std::chrono::duration_cast<Nanos>(t1 - t0).count());
        }
    }

    auto wall_end = Clock::now();
    double wall_s = std::chrono::duration_cast<Nanos>(wall_end - wall_start).count() / 1e9;
    double throughput = static_cast<double>(OPS) / wall_s;

    std::cout << "  Operations:    " << format_number(OPS) << "\n"
              << "  Duration:      " << std::fixed << std::setprecision(2) << wall_s << " seconds\n"
              << "  Throughput:    " << format_number(static_cast<int64_t>(throughput)) << " ops/sec\n"
              << "\n"
              << overall.summary()
              << std::string(72, '=') << "\n";

    std::cout << "\n=== JSON:scenario2 ===\n"
              << std::fixed << std::setprecision(0)
              << "{\"ops\":" << OPS
              << ",\"instruments\":" << NUM_INST
              << ",\"throughput\":" << throughput
              << ",\"p50_ns\":" << overall.percentile(50.0)
              << ",\"p99_ns\":" << overall.percentile(99.0)
              << ",\"p999_ns\":" << overall.percentile(99.9)
              << ",\"worst_ns\":" << overall.max()
              << "}\n";
}

// ═══════════════════════════════════════════════════════════════════════════
//  Scenario 3: Sustained Throughput
// ═══════════════════════════════════════════════════════════════════════════

static void scenario_3_sustained() {
    constexpr InstrumentId INST   = 200;
    constexpr PartitionId  PART   = 2;
    constexpr uint32_t POOL_SIZE  = 5000000;
    constexpr Price REF_PRICE     = 100000;
    constexpr int DURATION_S      = 10;  // 10s for reasonable runtime; set to 60 for full certification
    constexpr int WARMUP          = 10000;
    constexpr int INTERVALS       = 10;

    print_separator();
    std::cout << "  Matching Engine Benchmark -- Scenario 3 (Sustained Throughput)\n"
              << std::string(72, '=') << "\n";
    std::cout << "  Target:        1M ops/sec for " << DURATION_S << " seconds\n\n";

    JournalStub journal(POOL_SIZE * 2);
    Partition part(PART, POOL_SIZE, &journal);
    part.add_instrument(make_config(INST, REF_PRICE));

    ClientOrderId next_clord = 1;
    next_clord = populate_book(part, INST, REF_PRICE, 1000, 200, next_clord);

    std::mt19937 rng(99999);
    std::normal_distribution<double> price_dist(0.0, 40.0);
    std::uniform_int_distribution<int> qty_dist(1, 50);

    // Warmup
    for (int i = 0; i < WARMUP; ++i) {
        Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
        Price offset = static_cast<Price>(std::abs(price_dist(rng)));
        Price price = (side == Side::Buy) ? REF_PRICE - 5 - offset : REF_PRICE + 5 + offset;
        auto msg = make_new_order(INST, side, OrderType::Limit, price, qty_dist(rng), next_clord++);
        part.process(msg);
    }

    // Measure sustained throughput with periodic latency snapshots
    int interval_s = std::max(1, DURATION_S / INTERVALS);
    benchmark::LatencyHistogram overall;
    benchmark::LatencyHistogram interval_hist;

    std::cout << "  Time(s)  Ops         Throughput     p50        p99        p99.9\n"
              << "  " << std::string(68, '-') << "\n";

    auto total_start = Clock::now();
    int64_t total_ops = 0;

    for (int interval = 0; interval < INTERVALS; ++interval) {
        interval_hist.reset();
        auto interval_start = Clock::now();
        int64_t interval_ops = 0;

        while (true) {
            auto now = Clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - interval_start).count();
            if (elapsed >= interval_s * 1000) break;

            Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
            Price offset = static_cast<Price>(std::abs(price_dist(rng)));
            Price price = (side == Side::Buy) ? REF_PRICE - 5 - offset : REF_PRICE + 5 + offset;
            auto msg = make_new_order(INST, side, OrderType::Limit, price, qty_dist(rng), next_clord++);

            auto t0 = Clock::now();
            part.process(msg);
            auto t1 = Clock::now();

            int64_t lat = std::chrono::duration_cast<Nanos>(t1 - t0).count();
            interval_hist.record(lat);
            overall.record(lat);
            ++interval_ops;
        }

        total_ops += interval_ops;
        double int_tput = static_cast<double>(interval_ops) / interval_s;

        std::cout << "  " << std::setw(5) << (interval + 1) * interval_s
                  << "  " << std::setw(11) << format_number(interval_ops)
                  << "  " << std::setw(13) << format_number(static_cast<int64_t>(int_tput)) << "/s"
                  << "  " << std::setw(8) << benchmark::LatencyHistogram::format_ns(interval_hist.percentile(50.0))
                  << "  " << std::setw(8) << benchmark::LatencyHistogram::format_ns(interval_hist.percentile(99.0))
                  << "  " << benchmark::LatencyHistogram::format_ns(interval_hist.percentile(99.9))
                  << "\n";
    }

    auto total_end = Clock::now();
    double total_s = std::chrono::duration_cast<Nanos>(total_end - total_start).count() / 1e9;
    double final_tput = static_cast<double>(total_ops) / total_s;

    std::cout << "\n  Total ops:     " << format_number(total_ops) << "\n"
              << "  Total time:    " << std::fixed << std::setprecision(2) << total_s << " seconds\n"
              << "  Avg throughput:" << format_number(static_cast<int64_t>(final_tput)) << " ops/sec\n"
              << "\n"
              << overall.summary()
              << std::string(72, '=') << "\n";

    std::cout << "\n=== JSON:scenario3 ===\n"
              << std::fixed << std::setprecision(0)
              << "{\"total_ops\":" << total_ops
              << ",\"duration_s\":" << std::setprecision(2) << total_s
              << ",\"throughput\":" << std::setprecision(0) << final_tput
              << ",\"p50_ns\":" << overall.percentile(50.0)
              << ",\"p99_ns\":" << overall.percentile(99.0)
              << ",\"p999_ns\":" << overall.percentile(99.9)
              << ",\"mean_ns\":" << std::setprecision(1) << overall.mean()
              << "}\n";
}

// ═══════════════════════════════════════════════════════════════════════════
//  Scenario 4: Burst
// ═══════════════════════════════════════════════════════════════════════════

static void scenario_4_burst() {
    constexpr InstrumentId INST   = 300;
    constexpr PartitionId  PART   = 3;
    constexpr uint32_t POOL_SIZE  = 5000000;
    constexpr Price REF_PRICE     = 100000;
    constexpr int BURST_OPS       = 5000000;
    constexpr int WARMUP          = 10000;

    print_separator();
    std::cout << "  Matching Engine Benchmark -- Scenario 4 (Burst)\n"
              << std::string(72, '=') << "\n";
    std::cout << "  Target:        " << format_number(BURST_OPS) << " ops as fast as possible\n\n";

    JournalStub journal(POOL_SIZE * 2);
    Partition part(PART, POOL_SIZE, &journal);
    part.add_instrument(make_config(INST, REF_PRICE));

    ClientOrderId next_clord = 1;
    next_clord = populate_book(part, INST, REF_PRICE, 1000, 200, next_clord);

    // Pre-generate all messages to avoid measuring allocation
    std::mt19937 rng(54321);
    std::normal_distribution<double> price_dist(0.0, 40.0);
    std::uniform_int_distribution<int> qty_dist(1, 50);

    std::vector<PartitionMessage> messages;
    messages.reserve(WARMUP + BURST_OPS);

    for (int i = 0; i < WARMUP + BURST_OPS; ++i) {
        Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
        Price offset = static_cast<Price>(std::abs(price_dist(rng)));
        Price price = (side == Side::Buy) ? REF_PRICE - 5 - offset : REF_PRICE + 5 + offset;
        messages.push_back(make_new_order(INST, side, OrderType::Limit,
                                          price, qty_dist(rng), next_clord++));
    }

    // Warmup
    for (int i = 0; i < WARMUP; ++i) {
        part.process(messages[i]);
    }

    benchmark::LatencyHistogram hist;

    auto wall_start = Clock::now();

    for (int i = WARMUP; i < WARMUP + BURST_OPS; ++i) {
        auto t0 = Clock::now();
        part.process(messages[i]);
        auto t1 = Clock::now();
        hist.record(std::chrono::duration_cast<Nanos>(t1 - t0).count());
    }

    auto wall_end = Clock::now();
    double wall_s = std::chrono::duration_cast<Nanos>(wall_end - wall_start).count() / 1e9;
    double throughput = static_cast<double>(BURST_OPS) / wall_s;

    std::cout << "  Operations:    " << format_number(BURST_OPS) << "\n"
              << "  Duration:      " << std::fixed << std::setprecision(2) << wall_s << " seconds\n"
              << "  Throughput:    " << format_number(static_cast<int64_t>(throughput)) << " ops/sec\n"
              << "\n"
              << hist.summary()
              << std::string(72, '=') << "\n";

    std::cout << "\n=== JSON:scenario4 ===\n"
              << std::fixed << std::setprecision(0)
              << "{\"ops\":" << BURST_OPS
              << ",\"duration_s\":" << std::setprecision(2) << wall_s
              << ",\"throughput\":" << std::setprecision(0) << throughput
              << ",\"p50_ns\":" << hist.percentile(50.0)
              << ",\"p99_ns\":" << hist.percentile(99.0)
              << ",\"p999_ns\":" << hist.percentile(99.9)
              << ",\"p9999_ns\":" << hist.percentile(99.99)
              << ",\"worst_ns\":" << hist.max()
              << "}\n";
}

// ═══════════════════════════════════════════════════════════════════════════
//  Scenario 5: With Risk Checks
// ═══════════════════════════════════════════════════════════════════════════

static void scenario_5_with_risk() {
    constexpr InstrumentId INST   = 400;
    constexpr PartitionId  PART   = 4;
    constexpr uint32_t POOL_SIZE  = 2000000;
    constexpr int WARMUP          = 10000;
    constexpr int OPS             = 3000000;
    constexpr Price REF_PRICE     = 100000;

    print_separator();
    std::cout << "  Matching Engine Benchmark -- Scenario 5 (With Risk Checks)\n"
              << std::string(72, '=') << "\n";
    std::cout << "  Same as Scenario 1 but with BasicRiskService enabled\n\n";

    JournalStub journal(POOL_SIZE * 2);
    Partition part(PART, POOL_SIZE, &journal);
    part.add_instrument(make_config(INST, REF_PRICE));

    ClientOrderId next_clord = 1;
    next_clord = populate_book(part, INST, REF_PRICE, 1000, 375, next_clord);

    // Set up risk service with generous limits (we want to measure cost, not rejections)
    risk::BasicRiskService risk_svc;
    risk::RiskLimits limits;
    limits.max_order_qty = 10000000;
    limits.max_notional = 100000000.0;
    limits.max_price_deviation_pct = 50.0;
    limits.max_message_rate = 10000000;
    limits.max_open_orders = 10000000;
    risk_svc.set_default_limits(limits);

    std::mt19937 rng(11111);
    std::normal_distribution<double> price_dist(0.0, 50.0);
    std::uniform_int_distribution<int> qty_dist(1, 100);

    benchmark::LatencyHistogram overall;
    benchmark::LatencyHistogram risk_only;
    benchmark::LatencyHistogram engine_only;

    auto wall_start = Clock::now();

    for (int i = 0; i < WARMUP + OPS; ++i) {
        Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
        Price offset = static_cast<Price>(std::abs(price_dist(rng)));
        Price price = (side == Side::Buy) ? REF_PRICE - 5 - offset : REF_PRICE + 5 + offset;
        Quantity qty = qty_dist(rng);

        // Measure risk check
        risk::RiskRequest req{};
        req.client_order_id = next_clord;
        req.instrument_id = INST;
        req.participant_id = 1;
        req.account_id = 1;
        req.session_id = 1;
        req.side = side;
        req.order_type = OrderType::Limit;
        req.price = price;
        req.quantity = qty;
        req.reference_price = REF_PRICE;
        req.price_multiplier = 1;

        auto t0 = Clock::now();
        auto risk_result = risk_svc.check(req);
        auto t1 = Clock::now();

        int64_t risk_ns = std::chrono::duration_cast<Nanos>(t1 - t0).count();

        if (risk_result.result != risk::RiskResult::PASS) {
            if (i >= WARMUP) {
                risk_only.record(risk_ns);
                overall.record(risk_ns);
            }
            ++next_clord;
            continue;
        }

        // Measure engine processing
        auto msg = make_new_order(INST, side, OrderType::Limit, price, qty, next_clord++);

        auto t2 = Clock::now();
        part.process(msg);
        auto t3 = Clock::now();

        int64_t engine_ns = std::chrono::duration_cast<Nanos>(t3 - t2).count();
        int64_t total_ns = risk_ns + engine_ns;

        if (i >= WARMUP) {
            risk_only.record(risk_ns);
            engine_only.record(engine_ns);
            overall.record(total_ns);
        }
    }

    auto wall_end = Clock::now();
    double wall_s = std::chrono::duration_cast<Nanos>(wall_end - wall_start).count() / 1e9;
    double throughput = static_cast<double>(OPS) / wall_s;

    std::cout << "  Operations:    " << format_number(OPS) << "\n"
              << "  Duration:      " << std::fixed << std::setprecision(2) << wall_s << " seconds\n"
              << "  Throughput:    " << format_number(static_cast<int64_t>(throughput)) << " ops/sec\n"
              << "\n"
              << "  Combined (risk + engine):\n"
              << overall.summary()
              << "\n  Breakdown:\n";
    print_histogram_line("Risk:", risk_only);
    print_histogram_line("Engine:", engine_only);

    // Calculate overhead
    double risk_mean = risk_only.mean();
    double engine_mean = engine_only.mean();
    double overhead_pct = (engine_mean > 0) ? (risk_mean / engine_mean * 100.0) : 0.0;
    std::cout << "\n  Risk check overhead: "
              << std::fixed << std::setprecision(0) << risk_mean << " ns mean ("
              << std::setprecision(1) << overhead_pct << "% of engine latency)\n";

    std::cout << std::string(72, '=') << "\n";

    std::cout << "\n=== JSON:scenario5 ===\n"
              << std::fixed << std::setprecision(0)
              << "{\"ops\":" << OPS
              << ",\"throughput\":" << throughput
              << ",\"combined_p99_ns\":" << overall.percentile(99.0)
              << ",\"risk_p99_ns\":" << risk_only.percentile(99.0)
              << ",\"engine_p99_ns\":" << engine_only.percentile(99.0)
              << ",\"risk_mean_ns\":" << std::setprecision(1) << risk_mean
              << ",\"engine_mean_ns\":" << engine_mean
              << "}\n";
}

// ═══════════════════════════════════════════════════════════════════════════
//  Main
// ═══════════════════════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    // Parse optional scenario filter: ./matching-benchmark [1|2|3|4|5|all]
    int scenario = 0;  // 0 = run all
    if (argc > 1) {
        std::string arg(argv[1]);
        if (arg == "all") {
            scenario = 0;
        } else if (arg == "1" || arg == "2" || arg == "3" || arg == "4" || arg == "5") {
            scenario = std::atoi(argv[1]);
        } else {
            std::cerr << "Usage: " << argv[0] << " [1|2|3|4|5|all]\n"
                      << "  1 = Single instrument (exchange-core comparison)\n"
                      << "  2 = Multi-instrument (100 instruments)\n"
                      << "  3 = Sustained throughput (1M ops/sec target)\n"
                      << "  4 = Burst (5M ops as fast as possible)\n"
                      << "  5 = With risk checks (BasicRiskService)\n"
                      << "  all = Run all scenarios (default)\n";
            return 1;
        }
    }

    // Pin to core 0 if on Linux
    pin_to_core(0);

    std::cout << "\n" << std::string(72, '=') << "\n"
              << "  Matching Engine Benchmark Suite\n"
              << "  Build: " << __DATE__ << " " << __TIME__ << "\n"
#ifdef NDEBUG
              << "  Mode: Release\n"
#else
              << "  Mode: Debug (results not representative -- use Release)\n"
#endif
#ifdef __linux__
              << "  CPU pinning: enabled (core 0)\n"
#else
              << "  CPU pinning: disabled (not Linux)\n"
#endif
              << std::string(72, '=') << "\n";

    if (scenario == 0 || scenario == 1) scenario_1_single_instrument();
    if (scenario == 0 || scenario == 2) scenario_2_multi_instrument();
    if (scenario == 0 || scenario == 3) scenario_3_sustained();
    if (scenario == 0 || scenario == 4) scenario_4_burst();
    if (scenario == 0 || scenario == 5) scenario_5_with_risk();

    std::cout << "\n  NOTE: Results depend on hardware, OS scheduler, and build configuration.\n"
              << "  Production targets: Profile A engine p99 < 5 us (bare metal, CPU pinning, DPDK).\n"
              << "  Run with Release build (-DCMAKE_BUILD_TYPE=Release) for representative numbers.\n\n";

    return 0;
}
