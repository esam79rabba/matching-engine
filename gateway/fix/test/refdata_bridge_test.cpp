#include "gateway/refdata_gateway_bridge.h"
#include <gtest/gtest.h>

namespace gateway {
namespace {

refdata::RefDataService make_service() {
    refdata::RefDataService svc;

    engine::InstrumentConfig cfg{};
    cfg.instrument_id = 100;
    cfg.tick_size = 1; cfg.lot_size = 1;
    cfg.min_quantity = 1; cfg.max_quantity = 1000000;
    cfg.static_ref_price = 5000000;
    cfg.static_band_pct = 20.0;
    cfg.residual_policy = engine::ResidualPolicy::Cancel;
    cfg.stp_mode = engine::STPMode::CancelNewest;
    svc.add_instrument(cfg);
    svc.register_symbol("BTC-USD", 100);

    return svc;
}

TEST(RefDataBridgeTest, SymbolResolution) {
    auto svc = make_service();
    RefDataGatewayBridge bridge(svc);
    auto resolver = bridge.symbol_resolver();

    EXPECT_EQ(resolver("BTC-USD"), 100u);
    EXPECT_EQ(resolver("UNKNOWN"), 0u);
}

TEST(RefDataBridgeTest, ParsesViaRefDataAdapter) {
    auto svc = make_service();
    RefDataGatewayBridge bridge(svc);
    auto adapter = bridge.create_adapter();

    std::string fix = "8=FIX.4.4|35=D|11=1001|55=BTC-USD|54=1|40=2|44=50000|38=10|";
    auto cmd = adapter.parse_new_order(fix);
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->instrument_id, 100u);
    EXPECT_EQ(cmd->quantity, 10u);
}

TEST(RefDataBridgeTest, UnknownSymbolRejectsAtAdapter) {
    auto svc = make_service();
    RefDataGatewayBridge bridge(svc);
    auto adapter = bridge.create_adapter();

    std::string fix = "8=FIX.4.4|35=D|11=2001|55=UNKNOWN-PAIR|54=1|40=2|44=100|38=10|";
    auto cmd = adapter.parse_new_order(fix);
    EXPECT_FALSE(cmd.has_value());
}

}  // namespace
}  // namespace gateway
