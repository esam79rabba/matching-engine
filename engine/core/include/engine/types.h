#pragma once

#include <cstdint>

namespace engine {

// ─── Identifiers ────────────────────────────────────────────
using OrderId       = uint64_t;
using ClientOrderId = uint64_t;
using InstrumentId  = uint32_t;
using ParticipantId = uint32_t;
using AccountId     = uint32_t;
using SessionId     = uint32_t;
using PartitionId   = uint16_t;
using SequenceNum   = uint64_t;
using Timestamp     = uint64_t;   // nanoseconds since Unix epoch (UTC)
using TradeId       = uint64_t;
using ExecId        = uint64_t;

// ─── Integer-encoded price and quantity ─────────────────────
// All prices and quantities are stored as integers.
// Conversion: internal_value = external_decimal * multiplier
// The multiplier is per-instrument (from reference data).
using Price    = int64_t;
using Quantity = uint64_t;

// ─── Enums ──────────────────────────────────────────────────

enum class Side : uint8_t {
    Buy  = 1,
    Sell = 2
};

enum class OrderType : uint8_t {
    Market    = 1,
    Limit     = 2,
    // Stop = 3,       // Phase 2
    // StopLimit = 4,  // Phase 2
};

enum class TimeInForce : uint8_t {
    Day = 0,
    GTC = 1,
    GTD = 2,
    IOC = 3,
    FOK = 4,
    // AuctionOnly = 5,  // Phase 2
};

enum class OrderStatus : uint8_t {
    New             = 0,
    PartiallyFilled = 1,
    Filled          = 2,
    Cancelled       = 3,
    Rejected        = 4,
    Expired         = 5,
};

enum class InstrumentState : uint8_t {
    Inactive       = 0,
    PreOpen        = 1,
    Auction        = 2,  // Phase 2 (stretch): opening/volatility auction
    Continuous     = 3,
    Halted         = 4,
    Suspended      = 5,
    ClosingAuction = 6,  // Phase 2 (stretch)
    Closed         = 7,
    PostClose      = 8,
};

enum class STPMode : uint8_t {
    CancelNewest = 1,   // FR-1 default
    // CancelOldest = 2,  // Phase 2
    // CancelBoth = 3,    // Phase 2
};

enum class ResidualPolicy : uint8_t {
    Cancel              = 1,  // Default — cancel unfilled residual
    MarketToLimitCollar = 2,  // Convert with protection collar
};

enum class RejectCode : uint16_t {
    None                 = 0,
    UnknownInstrument    = 1,
    InvalidOrderType     = 2,
    PriceOutsideBands    = 3,
    QuantityExceedsMax   = 4,
    InvalidTick          = 5,
    InvalidLot           = 6,
    DuplicateClientOrder = 7,
    RateLimitExceeded    = 8,
    RiskCheckFailed      = 9,
    AccountNotFound      = 10,
    InstrumentHalted     = 11,
    FOKInsufficientQty   = 12,
    PostOnlyWouldMatch   = 13,
    MarketResidualCancel = 14,
    CollarOutsideBands   = 15,
    STPCancelled         = 16,
    IOCResidualCancel    = 17,
};

// ─── Order flags (bitfield) ─────────────────────────────────
constexpr uint16_t FLAG_POST_ONLY            = 0x01;
constexpr uint16_t FLAG_CANCEL_ON_DISCONNECT = 0x04;

}  // namespace engine
