// Enum / FIX-tag pinning test.
//
// Prevents silent drift between on-wire FIX tag values (what clients see)
// and the engine's internal enums (what matching logic depends on). A failed
// assertion here is a protocol break — treat it as a breaking change and
// bump the FIX adapter version rather than editing this test.

#include "engine/types.h"
#include "gateway/fix_parser.h"

#include <gtest/gtest.h>

#include <string>

// ─── Side (FIX tag 54) ────────────────────────────────────────────
static_assert(static_cast<int>(engine::Side::Buy)  == 1, "FIX 54=1 Buy");
static_assert(static_cast<int>(engine::Side::Sell) == 2, "FIX 54=2 Sell");

// ─── OrderType (FIX tag 40) ───────────────────────────────────────
static_assert(static_cast<int>(engine::OrderType::Market)       == 1, "FIX 40=1 Market");
static_assert(static_cast<int>(engine::OrderType::Limit)        == 2, "FIX 40=2 Limit");
static_assert(static_cast<int>(engine::OrderType::Stop)         == 3, "FIX 40=3 Stop");
static_assert(static_cast<int>(engine::OrderType::StopLimit)    == 4, "FIX 40=4 StopLimit");
static_assert(static_cast<int>(engine::OrderType::TrailingStop) == 5, "FIX 40=5 TrailingStop");

// ─── TimeInForce (FIX tag 59) ─────────────────────────────────────
static_assert(static_cast<int>(engine::TimeInForce::Day) == 0, "FIX 59=0 Day");
static_assert(static_cast<int>(engine::TimeInForce::GTC) == 1, "FIX 59=1 GTC");
static_assert(static_cast<int>(engine::TimeInForce::IOC) == 3, "FIX 59=3 IOC");
static_assert(static_cast<int>(engine::TimeInForce::FOK) == 4, "FIX 59=4 FOK");

// ─── OrderStatus (FIX tag 39) ─────────────────────────────────────
static_assert(static_cast<int>(engine::OrderStatus::New)             == 0, "FIX 39=0 New");
static_assert(static_cast<int>(engine::OrderStatus::PartiallyFilled) == 1, "FIX 39=1 PartiallyFilled");
static_assert(static_cast<int>(engine::OrderStatus::Filled)          == 2, "FIX 39=2 Filled");

// ─── Outbound ExecType strings (tag 150) ──────────────────────────
// RealFixAdapter emits these literals; a change here is a protocol break.
TEST(FixEnumPinning, OutboundExecTypeStringsAreStable) {
    gateway::RealFixAdapter adapter(
        /*symbol_resolver=*/[](const std::string&) { return engine::InstrumentId{100}; },
        /*price_converter=*/[](const std::string& s, engine::InstrumentId) { return static_cast<engine::Price>(std::stoll(s)); },
        /*qty_converter=*/[](const std::string& s, engine::InstrumentId) { return static_cast<engine::Quantity>(std::stoull(s)); });

    gateway::NewOrderCommand cmd{};
    cmd.client_order_id = 42;
    cmd.instrument_id   = 100;
    cmd.side            = engine::Side::Buy;
    cmd.price           = 10000;
    cmd.quantity        = 5;

    const std::string ack = adapter.build_new_ack(cmd, 777);
    EXPECT_NE(ack.find("150=0"), std::string::npos) << "New ack must emit 150=0";
    EXPECT_NE(ack.find("39=0"),  std::string::npos) << "New ack must emit 39=0";

    const std::string fill = adapter.build_fill(777, 10000, 2, 2, 3, engine::Side::Buy);
    EXPECT_NE(fill.find("150=F"), std::string::npos) << "Fill must emit 150=F";

    const std::string cx = adapter.build_cancel_ack(777);
    EXPECT_NE(cx.find("150=4"), std::string::npos) << "Cancel ack must emit 150=4";
    EXPECT_NE(cx.find("39=4"),  std::string::npos) << "Cancel ack must emit 39=4";

    const std::string rj = adapter.build_reject(42, engine::RejectCode::None);
    EXPECT_NE(rj.find("150=8"), std::string::npos) << "Reject must emit 150=8";
    EXPECT_NE(rj.find("39=8"),  std::string::npos) << "Reject must emit 39=8";
}
