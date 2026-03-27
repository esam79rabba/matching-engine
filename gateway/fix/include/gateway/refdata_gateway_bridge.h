#pragma once

#include "engine/types.h"
#include "refdata/ref_data_service.h"
#include "gateway/fix_parser.h"

#include <string>
#include <functional>

namespace gateway {

/// Bridge between reference data service and gateway adapters.
///
/// Provides the SymbolResolver, PriceConverter, and QtyConverter
/// functions that the RealFixAdapter needs, backed by the actual
/// RefDataService rather than test lambdas.
///
/// Generic — no asset-class-specific logic. The ref data service
/// provides multipliers and config; the bridge translates them
/// into the converter functions the adapter expects.
class RefDataGatewayBridge {
public:
    explicit RefDataGatewayBridge(const refdata::RefDataService& svc);

    /// Get a SymbolResolver function backed by the ref data service.
    [[nodiscard]] RealFixAdapter::SymbolResolver symbol_resolver() const;

    /// Get a PriceConverter function backed by the ref data service.
    [[nodiscard]] RealFixAdapter::PriceConverter price_converter() const;

    /// Get a QtyConverter function backed by the ref data service.
    [[nodiscard]] RealFixAdapter::QtyConverter qty_converter() const;

    /// Convenience: create a fully-wired RealFixAdapter.
    [[nodiscard]] RealFixAdapter create_adapter() const;

private:
    const refdata::RefDataService& svc_;
};

}  // namespace gateway
