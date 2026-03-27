#pragma once

#include "engine/types.h"
#include <cstdint>
#include <cstring>

namespace engine {

/// A journal entry — the unit of append to the write-ahead log.
///
/// Generic core structure. No asset-class-specific fields.
/// The payload is opaque bytes — the caller serializes the message
/// or event into the payload before appending.
///
/// Future production implementation will write these to NVMe with
/// fsync (Regulatory-Grade) or batched writes (Reduced-Latency).
/// This interface is the abstraction boundary for that swap.
struct JournalEntry {
    SequenceNum  sequence;        // Partition-local monotonic
    Timestamp    timestamp;       // PTP nanosecond timestamp
    PartitionId  partition_id;
    uint16_t     entry_type;      // Application-defined (input command vs output event)
    uint32_t     payload_size;
    const void*  payload;         // Points to caller-owned buffer (not copied by default)

    // Entry type constants
    static constexpr uint16_t TYPE_INPUT_COMMAND  = 1;  // Inbound message (new order, cancel, etc.)
    static constexpr uint16_t TYPE_OUTPUT_EVENT   = 2;  // Outbound event (fill, cancel confirm, etc.)
    static constexpr uint16_t TYPE_STATE_CHANGE   = 3;  // Instrument state transition
    static constexpr uint16_t TYPE_SNAPSHOT_MARKER = 4;  // Marks a snapshot point in the journal
};


/// Result of a journal append operation.
enum class JournalResult : uint8_t {
    OK              = 0,  // Entry appended (and durable if Regulatory-Grade)
    BUFFER_FULL     = 1,  // Journal buffer exhausted — backpressure
    IO_ERROR        = 2,  // Storage write failure
    SHUTDOWN        = 3,  // Journal is shutting down
};


/// Abstract journal interface.
///
/// The partition calls append() for every input and output.
/// The implementation determines durability semantics:
///   - Regulatory-Grade: fsync before returning OK
///   - Reduced-Latency: batch and fsync asynchronously
///   - Stub (development): in-memory only, no durability
///
/// This interface is the swap boundary. Production replaces the stub
/// with a real NVMe/Direct-I/O implementation (D-06 working assumption).
///
/// Design notes:
///   - Single-threaded per partition — no synchronization needed in impl.
///   - append() must not allocate heap memory on the hot path.
///   - The journal does not interpret payload contents.
class IJournal {
public:
    virtual ~IJournal() = default;

    /// Append an entry to the journal.
    /// In Regulatory-Grade mode, this blocks until durable (fsync).
    /// In Reduced-Latency mode, this returns immediately (batch pending).
    /// In stub mode, this stores in memory and returns immediately.
    virtual JournalResult append(const JournalEntry& entry) = 0;

    /// Return the current write position (sequence of last appended entry).
    [[nodiscard]] virtual SequenceNum last_sequence() const noexcept = 0;

    /// Return total entries appended since construction.
    [[nodiscard]] virtual uint64_t entry_count() const noexcept = 0;

    /// Iterate entries from a starting sequence for replay.
    /// Calls visitor(entry) for each entry in sequence order.
    /// Returns the number of entries visited.
    using Visitor = void(*)(const JournalEntry& entry, void* context);
    virtual uint64_t replay_from(SequenceNum from_seq, Visitor visitor, void* context) const = 0;

    /// Flush any pending batched writes (no-op for sync and stub modes).
    virtual void flush() = 0;
};

}  // namespace engine
