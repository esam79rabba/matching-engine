#pragma once

#include "engine/matching.h"
#include "engine/pipeline_event.h"
#include "engine/spsc_ring_buffer.h"
#include "engine/types.h"
#include "risk/risk_service.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <unordered_map>

namespace engine {

/// Pipelined matching engine processor (LMAX Disruptor-style).
///
/// Architecture:
///   [Gateway] -> input_to_risk_ -> [Risk Stage] -> risk_to_match_ ->
///   [Match Stage] -> match_to_output_ -> [Output Stage] -> callback
///
/// Each stage runs on its own thread with optional CPU affinity (Linux only).
/// Communication between stages uses lock-free SPSC ring buffers.
///
/// This is an opt-in alternative to the single-threaded Partition model.
/// The existing Partition code is NOT modified.
class PipelineProcessor {
public:
    /// Ring buffer capacity (must be power of 2).
    static constexpr size_t RING_BUFFER_CAPACITY = 65536;

    struct Config {
        bool pin_cores = false;    // CPU affinity (Linux only)
        int risk_core = -1;        // Core for risk stage (-1 = no pinning)
        int match_core = -1;       // Core for matching stage
        int output_core = -1;      // Core for output stage
    };

    /// Output callback type — invoked from the output stage thread.
    using OutputCallback = std::function<void(const PipelineEvent&)>;

    PipelineProcessor(Config config,
                      risk::IRiskService* risk_service,
                      std::unordered_map<InstrumentId, MatchingEngine>& engines);

    ~PipelineProcessor();

    // Non-copyable, non-movable (owns threads).
    PipelineProcessor(const PipelineProcessor&) = delete;
    PipelineProcessor& operator=(const PipelineProcessor&) = delete;

    /// Start all pipeline stage threads.
    void start();

    /// Stop all pipeline stage threads (waits for drain).
    void stop();

    /// Submit a message to the pipeline (called by gateway thread).
    /// Returns false if the input ring buffer is full (backpressure signal).
    bool submit(const PartitionMessage& msg);

    /// Set output callback (called from output stage thread).
    void set_output_callback(OutputCallback cb);

    /// Whether the pipeline is currently running.
    bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }

    // ─── Metrics ────────────────────────────────────────────────

    /// Total events that passed through the match stage.
    uint64_t events_processed() const noexcept {
        return events_processed_.load(std::memory_order_relaxed);
    }

    /// Total events rejected by the risk stage.
    uint64_t risk_rejects() const noexcept {
        return risk_rejects_.load(std::memory_order_relaxed);
    }

    /// Current fill level of the input ring buffer.
    size_t ring_buffer_usage() const {
        return input_to_risk_->available();
    }

private:
    Config config_;
    risk::IRiskService* risk_service_;
    std::unordered_map<InstrumentId, MatchingEngine>& engines_;

    // Ring buffers between stages. Heap-allocated: each is ~16 MB, so holding
    // them inline made the processor ~48 MB and overflowed the stack whenever
    // a PipelineProcessor was a local variable.
    using Ring = SPSCRingBuffer<PipelineEvent, RING_BUFFER_CAPACITY>;
    std::unique_ptr<Ring> input_to_risk_  = std::make_unique<Ring>();
    std::unique_ptr<Ring> risk_to_match_  = std::make_unique<Ring>();
    std::unique_ptr<Ring> match_to_output_ = std::make_unique<Ring>();

    // Stage threads.
    std::thread risk_thread_;
    std::thread match_thread_;
    std::thread output_thread_;

    std::atomic<bool> running_{false};
    OutputCallback output_callback_;

    // Metrics (relaxed ordering — counters, not synchronization).
    std::atomic<uint64_t> events_processed_{0};
    std::atomic<uint64_t> risk_rejects_{0};
    std::atomic<uint64_t> next_sequence_{1};

    // ─── Stage functions ────────────────────────────────────────

    void risk_stage();
    void match_stage();
    void output_stage();

    // ─── Helpers ────────────────────────────────────────────────

    /// Pin the calling thread to a specific CPU core.
    /// Linux only — no-op on macOS and other platforms.
    static void pin_to_core(int core_id);
};

}  // namespace engine
