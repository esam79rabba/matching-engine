#include "gateway/refdata_gateway_bridge.h"
#include <cstdlib>

namespace gateway {

RefDataGatewayBridge::RefDataGatewayBridge(const refdata::RefDataService& svc)
    : svc_(svc) {}

RealFixAdapter::SymbolResolver RefDataGatewayBridge::symbol_resolver() const {
    return [this](const std::string& symbol) -> engine::InstrumentId {
        return svc_.resolve_symbol(symbol);
    };
}

RealFixAdapter::PriceConverter RefDataGatewayBridge::price_converter() const {
    return [this](const std::string& val, engine::InstrumentId id) -> engine::Price {
        double decimal = std::stod(val);
        engine::Price multiplier = svc_.get_price_multiplier(id);
        return static_cast<engine::Price>(decimal * static_cast<double>(multiplier));
    };
}

RealFixAdapter::QtyConverter RefDataGatewayBridge::qty_converter() const {
    return [this](const std::string& val, engine::InstrumentId id) -> engine::Quantity {
        double decimal = std::stod(val);
        engine::Quantity multiplier = svc_.get_qty_multiplier(id);
        return static_cast<engine::Quantity>(decimal * static_cast<double>(multiplier));
    };
}

RealFixAdapter RefDataGatewayBridge::create_adapter() const {
    return RealFixAdapter(symbol_resolver(), price_converter(), qty_converter());
}

}  // namespace gateway
