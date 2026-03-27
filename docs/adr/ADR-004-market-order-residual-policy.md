# ADR-004: Market-Order Residual Policy

**Status**: DECIDED
**Date**: 2026-03-24
**Deciders**: Chief Architect, Market Microstructure Lead, CTO

## Context

When a market order sweeps all available liquidity in the order book and a residual quantity remains, the venue must decide what happens to the unfilled portion. The handling of this residual has significant implications for market integrity, participant risk, and regulatory scrutiny. An unsafe default policy can result in orders resting at prices far from fair value, creating settlement risk and erroneous execution complaints.

## Decision

When a market order exhausts available liquidity and residual quantity remains, venue configuration determines handling within the supported policy set:

1. **Cancel residual (default)** — The unfilled portion is immediately cancelled. An unsolicited cancel event is published.

2. **Market-to-limit with protection collar** — The residual converts to a limit order at the last fill price, subject to a configurable protection collar (e.g., ± N ticks or ± X% from reference price). If the collar would be breached, the residual is cancelled instead.

**Unprotected conversion of residual quantity into a resting limit order at the last fill price is NOT supported.**

The policy is configurable per venue rulebook within this supported set.

## Rationale

- **Cancel is the safest default**: After a book sweep, the last fill price may be far from fair value (especially in thin books). Resting an order at that price exposes the participant to adverse fills.
- **Collar provides controlled conversion**: For venues that want market-to-limit behavior, the collar bounds the risk. If the collar price would be outside the venue's price bands, the order is cancelled — providing a double safety net.
- **Unprotected resting is explicitly excluded**: This prevents a class of erroneous executions that are common on platforms that allow market orders to rest at arbitrary prices after a sweep.
- **Venue configurability**: Different market conventions may prefer different default policies. The platform supports both safe options via configuration.

## Consequences

- Market orders that exhaust the book will, by default, have their residual cancelled. Participants must be aware that market orders do not guarantee full fill.
- Venues that enable the collar option must configure the collar width (in ticks or percentage) as a venue rule parameter.
- The collar policy requires a last-fill price to exist. If a market order receives zero fills (empty book), the residual is always cancelled regardless of policy.
- Certification test CERT-007 verifies this behavior.

## Alternatives Considered

| Alternative | Why Rejected |
|------------|-------------|
| Rest at last fill price (unprotected) | Dangerous in thin books; common source of erroneous executions on other platforms |
| Convert to limit at best bid/offer | BBO may not exist after a sweep; introduces dependency on market data state |
| Always cancel (no collar option) | Too restrictive for some market conventions; collar provides controlled flexibility |
| Let participant choose per order | Adds per-order complexity; venue should enforce a consistent policy |

## Implementation Implications

- Market-order residual handling is a function call in the matching loop after the sweep completes
- Policy is stored as a book-level configuration parameter (`market_order_residual_policy`)
- Collar width is stored as `collar_ticks` (integer) on the book configuration
- Event emission: `OrderCancel` (reason=MARKET_RESIDUAL_CANCEL) or `OrderConvertedToLimit` (with collar_price)
- The pseudocode implementation is in §16 (engine matching algorithm draft)

## References

- §07 (MS-05 Price Protection), §11.4.3 (REQ-OT-001), §14 (FR-MTH-008), §23 (CERT-007), §30 (glossary: Market order)
