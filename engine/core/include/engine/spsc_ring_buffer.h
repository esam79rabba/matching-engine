#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>

namespace engine {

/// Lock-free Single-Producer Single-Consumer ring buffer.
/// Inspired by LMAX Disruptor's RingBuffer.
/// Cache-line padded to prevent false sharing between producer and consumer.
///
/// Template parameters:
///   T        — event type (must be trivially copyable for best performance)
///   Capacity — must be a power of 2
///
/// Memory ordering:
///   Producer writes data then publishes write_pos_ with release.
///   Consumer reads write_pos_ with acquire, then reads data.
///   This guarantees the consumer sees fully-written data.
template<typename T, size_t Capacity>
class SPSCRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of 2");
    static_assert(Capacity > 0, "Capacity must be positive");

public:
    SPSCRingBuffer() : buffer_{} {}

    /// Try to publish an event. Returns false if full (backpressure).
    /// Called by producer thread only.
    bool try_publish(const T& event) {
        const size_t wp = write_pos_.load(std::memory_order_relaxed);
        const size_t rp = read_pos_.load(std::memory_order_acquire);

        if ((wp - rp) >= Capacity) {
            return false;  // Buffer full
        }

        buffer_[wp & MASK] = event;
        write_pos_.store(wp + 1, std::memory_order_release);
        return true;
    }

    /// Try to consume an event. Returns false if empty.
    /// Called by consumer thread only.
    bool try_consume(T& event) {
        const size_t rp = read_pos_.load(std::memory_order_relaxed);
        const size_t wp = write_pos_.load(std::memory_order_acquire);

        if (rp >= wp) {
            return false;  // Buffer empty
        }

        event = buffer_[rp & MASK];
        read_pos_.store(rp + 1, std::memory_order_release);
        return true;
    }

    /// Check if buffer has events to consume (approximate — may race).
    bool has_available() const {
        const size_t rp = read_pos_.load(std::memory_order_acquire);
        const size_t wp = write_pos_.load(std::memory_order_acquire);
        return wp > rp;
    }

    /// Number of unconsumed events (approximate — may race).
    size_t available() const {
        const size_t rp = read_pos_.load(std::memory_order_acquire);
        const size_t wp = write_pos_.load(std::memory_order_acquire);
        return wp - rp;
    }

    /// Total capacity.
    static constexpr size_t capacity() { return Capacity; }

private:
    static constexpr size_t MASK = Capacity - 1;

    // Cache-line padding to prevent false sharing between producer and consumer.
    // Producer only writes write_pos_; consumer only writes read_pos_.
    alignas(64) std::atomic<size_t> write_pos_{0};
    char pad1_[64 - sizeof(std::atomic<size_t>)];

    alignas(64) std::atomic<size_t> read_pos_{0};
    char pad2_[64 - sizeof(std::atomic<size_t>)];

    alignas(64) T buffer_[Capacity];
};

}  // namespace engine
