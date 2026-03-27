#pragma once

#include "engine/types.h"
#include "gateway/fix_gateway_adapter.h"  // Reuse NewOrderCommand, CancelCommand, ModifyCommand

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace gateway {

/// SBE message header — common to all binary messages.
/// Matches the schema from 01-sbe-protocol-schema-draft.md.
struct SbeHeader {
    uint16_t block_length;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t version;
    uint64_t sequence_number;
    uint64_t sending_time;

    static constexpr size_t SIZE = 24;
};

/// SBE template IDs (from protocol schema draft).
namespace sbe_template {
    constexpr uint16_t NewOrder      = 1;
    constexpr uint16_t CancelOrder   = 2;
    constexpr uint16_t ModifyOrder   = 3;
    constexpr uint16_t ExecReport    = 10;
    constexpr uint16_t CancelReject  = 11;
    constexpr uint16_t Logon         = 20;
    constexpr uint16_t Logout        = 21;
    constexpr uint16_t Heartbeat     = 22;
}

/// SBE codec — encodes and decodes binary messages.
///
/// This is a hand-rolled codec matching the SBE schema draft.
/// In production, this would be generated from an SBE XML schema
/// using the SBE tool (https://github.com/real-logic/simple-binary-encoding).
///
/// The codec translates between raw byte buffers and the internal
/// command types (NewOrderCommand, CancelCommand, etc.) that are
/// shared with the FIX adapter. This ensures both gateways produce
/// identical internal commands regardless of wire protocol.
///
/// Generic — no asset-class-specific logic.
class SbeCodec {
public:

    // ─── Decode (wire → internal) ───────────────────────────

    /// Decode an SBE header from a byte buffer.
    /// Returns nullopt if buffer is too small.
    [[nodiscard]]
    static std::optional<SbeHeader> decode_header(const uint8_t* buf, size_t len);

    /// Decode a NewOrder message (template_id = 1).
    /// buf should point past the header.
    [[nodiscard]]
    static std::optional<NewOrderCommand> decode_new_order(const uint8_t* buf, size_t len);

    /// Decode a CancelOrder message (template_id = 2).
    [[nodiscard]]
    static std::optional<CancelCommand> decode_cancel(const uint8_t* buf, size_t len);

    /// Decode a ModifyOrder message (template_id = 3).
    [[nodiscard]]
    static std::optional<ModifyCommand> decode_modify(const uint8_t* buf, size_t len);

    // ─── Encode (internal → wire) ───────────────────────────

    /// Encode an execution report into a byte buffer.
    /// Returns the number of bytes written.
    static size_t encode_exec_report(uint8_t* buf, size_t buf_len,
                                      engine::OrderId order_id,
                                      engine::ClientOrderId client_order_id,
                                      engine::InstrumentId instrument_id,
                                      engine::Side side,
                                      uint8_t ord_status,
                                      uint8_t exec_type,
                                      engine::Price last_px,
                                      engine::Quantity last_qty,
                                      engine::Quantity cum_qty,
                                      engine::Quantity leaves_qty,
                                      uint64_t sequence,
                                      engine::Timestamp timestamp);

    /// Encode a heartbeat message.
    static size_t encode_heartbeat(uint8_t* buf, size_t buf_len,
                                    uint64_t sequence, engine::Timestamp timestamp);

private:
    /// Write header to buffer. Returns bytes written (always SbeHeader::SIZE).
    static size_t write_header(uint8_t* buf, uint16_t template_id,
                                uint16_t block_length, uint64_t sequence,
                                engine::Timestamp timestamp);

    /// Read a little-endian value from buffer.
    template<typename T>
    static T read_le(const uint8_t* buf) {
        T val;
        std::memcpy(&val, buf, sizeof(T));
        return val;
    }

    /// Write a little-endian value to buffer.
    template<typename T>
    static void write_le(uint8_t* buf, T val) {
        std::memcpy(buf, &val, sizeof(T));
    }
};

}  // namespace gateway
