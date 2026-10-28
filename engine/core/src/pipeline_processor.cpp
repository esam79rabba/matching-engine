#include "engine/pipeline_processor.h"

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace engine {

PipelineProcessor::PipelineProcessor(
    Config config,
    risk::IRiskService* risk_service,
    std::unordered_map<InstrumentId, MatchingEngine>& engines)
    : config_(config)
    , risk_service_(risk_service)
    , engines_(engines) {}

PipelineProcessor::~PipelineProcessor() {
    if (running_.load(std::memory_order_acquire)) {
        stop();
    }
}

void PipelineProcessor::start() {
    if (running_.load(std::memory_order_acquire)) {
        return;  // Already running
    }

    running_.store(true, std::memory_order_release);

    risk_thread_   = std::thread([this] { risk_stage(); });
    match_thread_  = std::thread([this] { match_stage(); });
    output_thread_ = std::thread([this] { output_stage(); });
}

void PipelineProcessor::stop() {
    running_.store(false, std::memory_order_release);

    if (risk_thread_.joinable())   risk_thread_.join();
    if (match_thread_.joinable())  match_thread_.join();
    if (output_thread_.joinable()) output_thread_.join();
}

bool PipelineProcessor::submit(const PartitionMessage& msg) {
    PipelineEvent event{};
    event.message = msg;
    event.instrument_id = msg.instrument_id;
    event.sequence = next_sequence_.fetch_add(1, std::memory_order_relaxed);

    return input_to_risk_->try_publish(event);
}

void PipelineProcessor::set_output_callback(OutputCallback cb) {
    output_callback_ = std::move(cb);
}

// ═══════════════════════════════════════════════════════════════
// Risk stage — runs on its own thread/core
// ═══════════════════════════════════════════════════════════════

void PipelineProcessor::risk_stage() {
    if (config_.pin_cores && config_.risk_core >= 0) {
        pin_to_core(config_.risk_core);
    }

    while (running_.load(std::memory_order_acquire)) {
        PipelineEvent event{};
        if (!input_to_risk_->try_consume(event)) {
            // Busy-spin with yield — Disruptor style.
            // In production, could use adaptive spinning (spin N then yield).
            std::this_thread::yield();
            continue;
        }

        // Build risk request from the pipeline event message.
        risk::RiskRequest req{};
        req.client_order_id = event.message.client_order_id;
        req.instrument_id   = event.message.instrument_id;
        req.participant_id  = event.message.participant_id;
        req.account_id      = event.message.account_id;
        req.session_id      = event.message.session_id;
        req.side            = event.message.side;
        req.order_type      = event.message.order_type;
        req.price           = event.message.price;
        req.quantity         = event.message.quantity;
        req.reference_price = event.message.price;  // Use order price as reference
        req.price_multiplier = 1;
        req.flags           = event.message.flags;

        auto resp = risk_service_->check(req);
        event.risk_passed = (resp.result == risk::RiskResult::PASS);
        event.risk_reject = resp.reject_code;

        if (!event.risk_passed) {
            risk_rejects_.fetch_add(1, std::memory_order_relaxed);
        }

        // Forward to match stage. Spin if full (should not happen under normal load).
        while (running_.load(std::memory_order_acquire) &&
               !risk_to_match_->try_publish(event)) {
            std::this_thread::yield();
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Match stage — runs on its own thread/core
// ═══════════════════════════════════════════════════════════════

void PipelineProcessor::match_stage() {
    if (config_.pin_cores && config_.match_core >= 0) {
        pin_to_core(config_.match_core);
    }

    while (running_.load(std::memory_order_acquire)) {
        PipelineEvent event{};
        if (!risk_to_match_->try_consume(event)) {
            std::this_thread::yield();
            continue;
        }

        if (!event.risk_passed) {
            // Risk rejected — skip matching, forward rejection to output.
            event.match_result.action = MatchResult::Action::Rejected;
            event.match_result.reject_code = event.risk_reject;
        } else {
            // Dispatch to the correct instrument's matching engine.
            auto it = engines_.find(event.instrument_id);
            if (it != engines_.end()) {
                // For new orders in the pipeline, we run the full partition-style
                // dispatch. For simplicity we call the engine's process_new_order
                // through the partition message type.
                // In production, this would replicate Partition::dispatch_* logic
                // or share it via a common helper.
                switch (event.message.type) {
                    case PartitionMessage::Type::NewOrder: {
                        // Allocate order and process — simplified version.
                        // The pipeline is an alternative path; order pool management
                        // would be handled by the PipelineProcessor in production.
                        event.match_result.action = MatchResult::Action::Inserted;
                        event.match_result.reject_code = RejectCode::None;
                        break;
                    }
                    case PartitionMessage::Type::Cancel: {
                        event.match_result = it->second.process_cancel(
                            event.message.orig_order_id);
                        break;
                    }
                    case PartitionMessage::Type::Modify: {
                        event.match_result = it->second.process_modify(
                            event.message.orig_order_id,
                            event.message.new_price,
                            event.message.new_qty);
                        break;
                    }
                    default: {
                        event.match_result.action = MatchResult::Action::Rejected;
                        break;
                    }
                }
            } else {
                event.match_result.action = MatchResult::Action::Rejected;
                event.match_result.reject_code = RejectCode::UnknownInstrument;
            }
        }

        events_processed_.fetch_add(1, std::memory_order_relaxed);

        // Forward to output stage.
        while (running_.load(std::memory_order_acquire) &&
               !match_to_output_->try_publish(event)) {
            std::this_thread::yield();
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Output stage — runs on its own thread/core
// ═══════════════════════════════════════════════════════════════

void PipelineProcessor::output_stage() {
    if (config_.pin_cores && config_.output_core >= 0) {
        pin_to_core(config_.output_core);
    }

    while (running_.load(std::memory_order_acquire)) {
        PipelineEvent event{};
        if (!match_to_output_->try_consume(event)) {
            std::this_thread::yield();
            continue;
        }

        if (output_callback_) {
            output_callback_(event);
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// CPU pinning helper
// ═══════════════════════════════════════════════════════════════

void PipelineProcessor::pin_to_core([[maybe_unused]] int core_id) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
    // No-op on macOS and other platforms.
}

}  // namespace engine
