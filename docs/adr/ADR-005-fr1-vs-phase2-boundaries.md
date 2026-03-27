# ADR-005: Foundation Release (FR-1) vs Phase 2+ Boundary Assumptions

**Status**: DECIDED
**Date**: 2026-03-24
**Deciders**: Chief Architect, Product Manager, CTO, Program Manager

## Context

The matching engine platform is planned for phased delivery. The Foundation Release (FR-1) must be a production-capable release with a clearly defined scope boundary. Features deferred to Phase 2+ must not create architectural debt that prevents their later addition. Conversely, FR-1 must not attempt to deliver Enterprise-grade capabilities that would delay time-to-market.

## Decision

### FR-1 Includes

| Area | FR-1 Scope |
|------|-----------|
| Matching | Continuous LOB; price-time FIFO; opening/closing auctions (stretch goal) |
| Order types | Market, Limit, IOC, FOK, GTC, GTD, Day |
| STP | Cancel-newest mode |
| Residual policy | Cancel (default); market-to-limit with collar |
| Connectivity | FIX 4.4 gateway (1 HA pair); binary SBE gateway (1 pair); drop copy (FIX) |
| Risk | Fat-finger (price, qty, notional); message rate throttle; kill switch |
| Market data | Multicast depth + trades + statistics; TCP snapshot recovery |
| Post-trade | Trade capture; fee calculation; trade confirmation; EOD files |
| Surveillance | File-based event export (order/trade lifecycle) |
| Operations | Basic console (session start/stop, halt/resume, parameter view) |
| Audit | Journal-based audit trail; basic query tool |
| Compliance | Single jurisdiction profile; audit trail; clock sync evidence |
| Recovery | Warm restart from snapshot + journal (single site) |
| Monitoring | Basic metrics + dashboards + alerting |
| Certification | 25 mandatory test cases; basic cert environment |

### Phase 2+ Defers

| Area | Deferred | Phase |
|------|----------|:-----:|
| Volatility auctions / circuit breakers | Complex; safe stretch goal for FR-1 | 2 |
| Iceberg, pegged, stop, stop-limit orders | Advanced order types | 2 |
| Pro-rata, parity allocation | Alternative priority models | 3 |
| Mass quote gateway (dedicated) | Market maker optimization | 2 |
| WebSocket / REST gateway | Lower-tier participant access | 2 |
| Credit limits, position limits, exposure limits | Advanced risk controls | 2 |
| Short-sell controls | Equity-specific | 2 |
| CCP clearing adapter | Post-trade integration | 2 |
| Allocations, give-up/take-up | Post-trade workflows | 2 |
| Settlement instruction generation | CSD integration | 2 |
| Real-time surveillance stream | Kafka-based pattern detection | 2 |
| MM obligation monitoring | Market maker compliance | 2 |
| Four-eyes approval (ops console) | Operational governance | 2 |
| Replay viewer | Historical reconstruction | 2 |
| Reconciliation tools | Internal/external matching | 2 |
| Active-passive DR site | Disaster recovery | 2 |
| Journal replication to DR | DR data protection | 2 |
| RFQ workflow | Fixed income / OTF trading | 3 |
| Dark / midpoint matching | Equity dark pool | 3 |
| Complex / strategy orders | Derivatives multi-leg | 3 |
| Implied matching | Derivatives optimization | 4 |
| Automated DR failover | Advanced resilience | 4 |
| DPDK gateway optimization | Advanced performance | 4 |
| Custody / wallet adapter | Digital assets | 5 |
| Multi-tenant deployment | Platform licensing | 5 |

## Rationale

- **FR-1 must be production-capable**: It can run a trading day, execute trades, calculate fees, and produce a complete audit trail. It is not a prototype.
- **Architecture supports additive phases**: Every Phase 2+ feature is designed to be additive — plugging into existing interfaces — without re-architecture of the core engine.
- **Scope discipline prevents FR-1 overload**: Including too many features in FR-1 would delay time-to-market and increase delivery risk.
- **Deferred features have defined slots**: Each deferred feature has a target phase, ensuring stakeholders know when to expect it.

## Consequences

- FR-1 has no circuit breaker protection (unless auctions are delivered as stretch goal). This is an accepted risk documented in the FR-1 vs Enterprise comparison.
- Market makers must use FIX for quoting in FR-1 (no dedicated mass-quote gateway). This may slow market-maker onboarding.
- No DR site in FR-1 means extended downtime (warm restart only) on hardware failure. Accepted for pilot/soft launch.
- No clearing integration in FR-1 means manual post-trade processing until Phase 2 CCP adapter is delivered.

## Alternatives Considered

| Alternative | Why Rejected |
|------------|-------------|
| Ship everything in one release | 18–24 months minimum; delays revenue; increases risk |
| Ship matching-only (no gateways, no ops) | Not production-capable; participants cannot connect |
| Include DR in FR-1 | Adds 2–3 months and significant infrastructure cost; warm restart is acceptable for initial deployment |

## Implementation Implications

- All Phase 2+ features must have defined interfaces/hooks in FR-1 code (e.g., abstract journal replication interface, surveillance stream interface, risk check interface) — even if the FR-1 implementation is a stub.
- The stub/mock strategy in the development bootstrap plan ensures Phase 2 features can be plugged in without engine re-architecture.
- FR-1 acceptance criteria (Milestone 1 and go-live) are defined against FR-1 scope only.

## References

- §10 (Functional Scope by Release Phase), §26 (Implementation Roadmap), §29 (FR-1 vs Enterprise)
