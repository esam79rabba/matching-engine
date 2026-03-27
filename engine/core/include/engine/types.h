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
    Stop      = 3,
    StopLimit = 4,
};

enum class TimeInForce : uint8_t {
    Day = 0,
    GTC = 1,
    GTD = 2,
    IOC = 3,
    FOK = 4,
    AuctionOnly = 5,  // Only valid during auction/pre-open states
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
    CancelNewest = 1,   // Cancel the aggressive (incoming) order
    CancelOldest = 2,   // Cancel the passive (resting) order
    CancelBoth   = 3,   // Cancel both sides
    Decrement    = 4,   // Reduce incoming qty by resting qty
};

enum class MatchingAlgorithm : uint8_t {
    PriceTimeFIFO = 1,  // Default: price-time priority (FIFO within level)
    ProRata       = 2,  // Proportional allocation within price level
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
    StopPriceMissing     = 18,
    StopPriceInvalid     = 19,
    PositionLimitBreached = 20,
    CreditLimitBreached  = 21,
    PegNotLimitOrder     = 22,
    PegNoReference       = 23,
    ReduceOnlyWouldIncrease = 24,
};

enum class PegType : uint8_t {
    None       = 0,
    Primary    = 1,  // Same-side BBO (buy→best_bid, sell→best_ask)
    Midpoint   = 2,  // Midpoint of BBO
    Market     = 3,  // Opposite-side BBO (buy→best_ask, sell→best_bid)
};

// ─── Order flags (bitfield) ─────────────────────────────────
constexpr uint16_t FLAG_POST_ONLY            = 0x01;
constexpr uint16_t FLAG_ICEBERG              = 0x02;
constexpr uint16_t FLAG_CANCEL_ON_DISCONNECT = 0x04;
constexpr uint16_t FLAG_REDUCE_ONLY          = 0x08;

}  // namespace engine
