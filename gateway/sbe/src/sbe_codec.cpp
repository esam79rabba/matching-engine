#include "gateway/sbe_codec.h"

namespace gateway {

// ═══════════════════════════════════════════════════════════════
// Header
// ═══════════════════════════════════════════════════════════════

size_t SbeCodec::write_header(uint8_t* buf, uint16_t template_id,
                               uint16_t block_length, uint64_t sequence,
                               engine::Timestamp timestamp) {
    write_le<uint16_t>(buf + 0, block_length);
    write_le<uint16_t>(buf + 2, template_id);
    write_le<uint16_t>(buf + 4, 1);   // schema_id
    write_le<uint16_t>(buf + 6, 1);   // version
    write_le<uint64_t>(buf + 8, sequence);
    write_le<uint64_t>(buf + 16, timestamp);
    return SbeHeader::SIZE;
}

std::optional<SbeHeader> SbeCodec::decode_header(const uint8_t* buf, size_t len) {
    if (len < SbeHeader::SIZE) return std::nullopt;

    SbeHeader hdr{};
    hdr.block_length   = read_le<uint16_t>(buf + 0);
    hdr.template_id    = read_le<uint16_t>(buf + 2);
    hdr.schema_id      = read_le<uint16_t>(buf + 4);
    hdr.version        = read_le<uint16_t>(buf + 6);
    hdr.sequence_number = read_le<uint64_t>(buf + 8);
    hdr.sending_time   = read_le<uint64_t>(buf + 16);
    return hdr;
}

// ═══════════════════════════════════════════════════════════════
// Decode — NewOrder (template_id = 1, body = 57 bytes)
// ═══════════════════════════════════════════════════════════════

std::optional<NewOrderCommand> SbeCodec::decode_new_order(const uint8_t* buf, size_t len) {
    constexpr size_t BODY_SIZE = 57;
    if (len < BODY_SIZE) return std::nullopt;

    NewOrderCommand cmd{};
    size_t off = 0;

    cmd.client_order_id = read_le<uint64_t>(buf + off); off += 8;
    cmd.instrument_id   = read_le<uint32_t>(buf + off); off += 4;
    cmd.side            = static_cast<engine::Side>(read_le<uint8_t>(buf + off)); off += 1;
    cmd.order_type      = static_cast<engine::OrderType>(read_le<uint8_t>(buf + off)); off += 1;
    cmd.time_in_force   = static_cast<engine::TimeInForce>(read_le<uint8_t>(buf + off)); off += 1;
    cmd.price           = read_le<int64_t>(buf + off);  off += 8;
    cmd.quantity         = read_le<uint64_t>(buf + off); off += 8;
    /* display_qty */     off += 8;  // Skip for FR-1 (iceberg is Phase 2)
    /* stop_price */      off += 8;  // Skip for FR-1
    cmd.stp_group_id    = read_le<uint32_t>(buf + off); off += 4;
    cmd.account_id      = read_le<uint32_t>(buf + off); off += 4;
    cmd.flags           = read_le<uint16_t>(buf + off); off += 2;

    cmd.participant_id = 1;  // TODO: resolve from session
    cmd.session_id = 1;

    return cmd;
}

// ═══════════════════════════════════════════════════════════════
// Decode — CancelOrder (template_id = 2, body = 20 bytes)
// ═══════════════════════════════════════════════════════════════

std::optional<CancelCommand> SbeCodec::decode_cancel(const uint8_t* buf, size_t len) {
    constexpr size_t BODY_SIZE = 20;
    if (len < BODY_SIZE) return std::nullopt;

    CancelCommand cmd{};
    cmd.orig_order_id   = read_le<uint64_t>(buf + 0);
    cmd.client_order_id = read_le<uint64_t>(buf + 8);
    cmd.instrument_id   = read_le<uint32_t>(buf + 16);
    return cmd;
}

// ═══════════════════════════════════════════════════════════════
// Decode — ModifyOrder (template_id = 3, body = 36 bytes)
// ═══════════════════════════════════════════════════════════════

std::optional<ModifyCommand> SbeCodec::decode_modify(const uint8_t* buf, size_t len) {
    constexpr size_t BODY_SIZE = 36;
    if (len < BODY_SIZE) return std::nullopt;

    ModifyCommand cmd{};
    cmd.orig_order_id   = read_le<uint64_t>(buf + 0);
    cmd.client_order_id = read_le<uint64_t>(buf + 8);
    cmd.instrument_id   = read_le<uint32_t>(buf + 16);
    cmd.new_price       = read_le<int64_t>(buf + 20);
    cmd.new_quantity     = read_le<uint64_t>(buf + 28);
    return cmd;
}

// ═══════════════════════════════════════════════════════════════
// Encode — ExecutionReport (template_id = 10)
// ═══════════════════════════════════════════════════════════════

size_t SbeCodec::encode_exec_report(
        uint8_t* buf, size_t buf_len,
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
        engine::Timestamp timestamp) {

    constexpr size_t BODY_SIZE = 82;
    constexpr size_t TOTAL = SbeHeader::SIZE + BODY_SIZE;
    if (buf_len < TOTAL) return 0;

    size_t off = write_header(buf, sbe_template::ExecReport, BODY_SIZE, sequence, timestamp);

    write_le<uint64_t>(buf + off, order_id);         off += 8;
    write_le<uint64_t>(buf + off, client_order_id);  off += 8;
    write_le<uint64_t>(buf + off, 0);                off += 8;  // exec_id (placeholder)
    write_le<uint64_t>(buf + off, 0);                off += 8;  // trade_id (placeholder)
    write_le<uint32_t>(buf + off, instrument_id);    off += 4;
    write_le<uint8_t>(buf + off, static_cast<uint8_t>(side)); off += 1;
    write_le<uint8_t>(buf + off, ord_status);        off += 1;
    write_le<uint8_t>(buf + off, exec_type);         off += 1;
    write_le<int64_t>(buf + off, last_px);           off += 8;
    write_le<uint64_t>(buf + off, last_qty);         off += 8;
    write_le<uint64_t>(buf + off, cum_qty);          off += 8;
    write_le<uint64_t>(buf + off, leaves_qty);       off += 8;
    write_le<uint8_t>(buf + off, 0);                 off += 1;  // liquidity_ind
    write_le<uint16_t>(buf + off, 0);                off += 2;  // reject_reason
    write_le<uint64_t>(buf + off, timestamp);        off += 8;

    return TOTAL;
}

// ═══════════════════════════════════════════════════════════════
// Encode — Heartbeat (template_id = 22)
// ═══════════════════════════════════════════════════════════════

size_t SbeCodec::encode_heartbeat(uint8_t* buf, size_t buf_len,
                                    uint64_t sequence, engine::Timestamp timestamp) {
    constexpr size_t TOTAL = SbeHeader::SIZE;  // Heartbeat has no body
    if (buf_len < TOTAL) return 0;
    write_header(buf, sbe_template::Heartbeat, 0, sequence, timestamp);
    return TOTAL;
}

}  // namespace gateway
