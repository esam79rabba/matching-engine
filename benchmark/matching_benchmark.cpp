/// Matching engine benchmark harness — prototype.
///
/// This is an early-stage benchmark for development profiling.
/// It is NOT an exchange-grade performance certification tool.
/// Results are indicative of relative performance, not absolute
/// production latency (which depends on deployment profile hardware).
///
/// Measures:
///   1. Insert latency (limit order into empty/populated book)
///   2. Match latency (aggressive order crossing resting liquidity)
///   3. Cancel latency
///   4. Throughput (sustained operations per second)

#include "engine/partition.h"
#include "engine/journal_stub.h"
#include "engine/matching.h"

#include <chrono>
#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>
#include <iomanip>
#include <cstdint>

using namespace engine;
using Clock = std::chrono::high_resolution_clock;
using Nanos = std::chrono::nanoseconds;

// ─── Configuration ──────────────────────────────────────────

constexpr uint32_t WARMUP_OPS    = 10000;
constexpr uint32_t MEASURE_OPS   = 100000;
constexpr InstrumentId INST_ID   = 100;
constexpr PartitionId  PART_ID   = 1;
constexpr uint32_t POOL_SIZE     = 500000;

// ─── Helpers ────────────────────────────────────────────────

InstrumentConfig make_benchmark_config() {
    InstrumentConfig cfg{};
    cfg.instrument_id = INST_ID;
    cfg.tick_size = 1;
    cfg.lot_size = 1;
    cfg.min_quantity = 1;
    cfg.max_quantity = 10000000;
    cfg.static_ref_price = 100000;  // Reference: 1000.00 at multiplier 100
    cfg.static_band_pct = 50.0;     // Wide band for benchmark
    cfg.dynamic_band_pct = 50.0;
    cfg.residual_policy = ResidualPolicy::Cancel;
    cfg.collar_ticks = 100;
    cfg.stp_mode = STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    return cfg;
}

PartitionMessage make_limit_order(Side side, Price price, Quantity qty, ClientOrderId clord) {
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::NewOrder;
    msg.timestamp = 0;
    msg.client_order_id = clord;
    msg.instrument_id = INST_ID;
    msg.side = side;
    msg.order_type = OrderType::Limit;
    msg.time_in_force = TimeInForce::Day;
    msg.price = price;
    msg.quantity = qty;
    msg.participant_id = 1;
    msg.account_id = 1;
    msg.session_id = 1;
    return msg;
}

struct BenchmarkResult {
    std::string name;
    uint32_t    ops;
    double      avg_ns;
    double      p50_ns;
    double      p99_ns;
    double      p999_ns;
    double      throughput;  // ops/sec
};

BenchmarkResult run_latency_benchmark(
        const std::string& name,
        Partition& partition,
        const std::vector<PartitionMessage>& messages,
        uint32_t warmup, uint32_t measure) {

    std::vector<int64_t> latencies;
    latencies.reserve(measure);

    uint32_t total = warmup + measure;
    uint32_t msg_count = static_cast<uint32_t>(messages.size());

    for (uint32_t i = 0; i < total; ++i) {
        const auto& msg = messages[i % msg_count];

        auto start = Clock::now();
        partition.process(msg);
        auto end = Clock::now();

        if (i >= warmup) {
            latencies.push_back(
                std::chrono::duration_cast<Nanos>(end - start).count());
        }
    }

    std::sort(latencies.begin(), latencies.end());

    auto percentile = [&](double p) -> double {
        size_t idx = static_cast<size_t>(p * latencies.size());
        if (idx >= latencies.size()) idx = latencies.size() - 1;
        return static_cast<double>(latencies[idx]);
    };

    double avg = std::accumulate(latencies.begin(), latencies.end(), 0.0)
                 / static_cast<double>(latencies.size());

    double total_time_s = std::accumulate(latencies.begin(), latencies.end(), 0.0) / 1e9;
    double throughput = static_cast<double>(latencies.size()) / total_time_s;

    return {
        name,
        static_cast<uint32_t>(latencies.size()),
        avg,
        percentile(0.50),
        percentile(0.99),
        percentile(0.999),
        throughput
    };
}

void print_result(const BenchmarkResult& r) {
    std::cout << std::left << std::setw(35) << r.name
              << "  ops=" << std::setw(8) << r.ops
              << "  avg=" << std::setw(8) << std::fixed << std::setprecision(0) << r.avg_ns << "ns"
              << "  p50=" << std::setw(8) << r.p50_ns << "ns"
              << "  p99=" << std::setw(8) << r.p99_ns << "ns"
              << "  p99.9=" << std::setw(8) << r.p999_ns << "ns"
              << "  tput=" << std::setw(10) << std::setprecision(0) << r.throughput << " ops/s"
              << "\n";
}

// ─── Benchmarks ─────────────────────────────────────────────

BenchmarkResult bench_insert_empty_book() {
    JournalStub journal(POOL_SIZE * 3);
    Partition part(PART_ID, POOL_SIZE, &journal);
    part.add_instrument(make_benchmark_config());

    // Alternating buy/sell at different prices so book grows
    std::vector<PartitionMessage> messages;
    for (uint32_t i = 0; i < WARMUP_OPS + MEASURE_OPS; ++i) {
        Side side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        Price price = (side == Side::Buy) ? (90000 + (i % 1000)) : (110000 + (i % 1000));
        messages.push_back(make_limit_order(side, price, 100, i + 1));
    }

    return run_latency_benchmark("Insert (growing book)", part, messages, WARMUP_OPS, MEASURE_OPS);
}

BenchmarkResult bench_match_single_level() {
    JournalStub journal(POOL_SIZE * 4);
    Partition part(PART_ID, POOL_SIZE, &journal);
    part.add_instrument(make_benchmark_config());

    // Pre-populate: 1000 resting sells at 100000
    for (uint32_t i = 0; i < 1000; ++i) {
        part.process(make_limit_order(Side::Sell, 100000, 10, i + 1));
    }

    // Benchmark: aggressive buys that match
    std::vector<PartitionMessage> messages;
    for (uint32_t i = 0; i < WARMUP_OPS + MEASURE_OPS; ++i) {
        messages.push_back(make_limit_order(Side::Buy, 100000, 1, 10000 + i));
    }

    return run_latency_benchmark("Match (single level)", part, messages, WARMUP_OPS, MEASURE_OPS);
}

BenchmarkResult bench_cancel() {
    JournalStub journal(POOL_SIZE * 4);
    Partition part(PART_ID, POOL_SIZE, &journal);
    part.add_instrument(make_benchmark_config());

    // Insert orders, then cancel them
    uint32_t total = WARMUP_OPS + MEASURE_OPS;
    for (uint32_t i = 0; i < total; ++i) {
        part.process(make_limit_order(Side::Buy, 90000 + (i % 1000), 100, i + 1));
    }

    // Build cancel messages
    std::vector<PartitionMessage> cancels;
    for (uint32_t i = 0; i < total; ++i) {
        PartitionMessage msg{};
        msg.type = PartitionMessage::Type::Cancel;
        msg.timestamp = 0;
        msg.orig_order_id = (static_cast<uint64_t>(PART_ID) << 48) | (i + 1);
        msg.instrument_id = INST_ID;
        cancels.push_back(msg);
    }

    return run_latency_benchmark("Cancel (populated book)", part, cancels, WARMUP_OPS, MEASURE_OPS);
}

// ─── Main ───────────────────────────────────────────────────

int main() {
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  Matching Engine Benchmark — PROTOTYPE (not exchange-grade)\n";
    std::cout << "  Warmup: " << WARMUP_OPS << " ops  |  Measure: " << MEASURE_OPS << " ops\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n\n";

    auto r1 = bench_insert_empty_book();
    print_result(r1);

    auto r2 = bench_match_single_level();
    print_result(r2);

    auto r3 = bench_cancel();
    print_result(r3);

    std::cout << "\n  NOTE: These are prototype numbers on development hardware.\n"
              << "  Production targets depend on deployment profile (A/B/C).\n"
              << "  Profile A target: engine p99 < 5 µs (bare metal, CPU pinning, DPDK).\n";

    return 0;
}
