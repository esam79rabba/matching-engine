# Repository and Module Structure — Draft

**Status**: Starter artifact
**Assumption**: Monorepo layout (provisional; can split later if needed)
**Note**: Language-specific build tooling TBD after D-01 closes

---

## Top-Level Layout

```
matching-engine/
├── engine/                     # Matching engine core (hot path)
│   ├── core/                   # Order book, matching logic, state machine
│   ├── sequencer/              # Sequencer module
│   ├── journal/                # Journal/WAL implementation
│   ├── snapshot/               # Snapshot capture and restore
│   └── tests/                  # Engine unit + simulation tests
│
├── protocol/                   # Wire protocol definitions
│   ├── sbe/                    # SBE binary protocol schema + codegen
│   ├── fix/                    # FIX message mapping and config
│   └── internal/               # Internal event schema (event taxonomy)
│
├── gateway/                    # Connectivity gateways
│   ├── fix-gateway/            # FIX 4.4 order entry + drop copy
│   ├── binary-gateway/         # SBE binary low-latency gateway
│   └── shared/                 # Session state, throttling, auth, CoD
│
├── risk/                       # Pre-trade risk service
│   ├── checks/                 # Fat finger, notional, rate limit
│   ├── killswitch/             # Kill switch logic
│   └── config/                 # Limit configuration and API
│
├── market-data/                # Market data publisher
│   ├── publisher/              # Multicast publisher
│   ├── snapshot-service/       # TCP snapshot recovery
│   └── encoding/               # SBE market data encoding
│
├── reference-data/             # Reference data service
│   ├── service/                # CRUD API, bulk load, versioning
│   ├── model/                  # Data model (instrument, participant, etc.)
│   └── admin-api/              # REST admin API
│
├── post-trade/                 # Post-trade service
│   ├── capture/                # Trade capture from output bus
│   ├── fees/                   # Fee calculation engine
│   ├── confirmation/           # Trade confirmation messages
│   └── eod/                    # End-of-day file generation
│
├── surveillance/               # Surveillance hooks
│   ├── export/                 # Event export pipeline (file-based FR-1)
│   └── audit/                  # Audit trail query tool
│
├── ops/                        # Operations tooling
│   ├── console/                # Web-based operations console
│   ├── replay/                 # Replay tooling
│   └── reconciliation/         # Reconciliation tools
│
├── security/                   # Security infrastructure
│   ├── pki/                    # Certificate management
│   ├── rbac/                   # Role-based access control
│   └── secrets/                # Secrets management integration
│
├── infra/                      # Infrastructure and deployment
│   ├── deploy/                 # Deployment scripts/configs
│   ├── monitoring/             # Prometheus rules, Grafana dashboards
│   ├── ptp/                    # PTP configuration
│   └── tuning/                 # OS/kernel tuning scripts
│
├── testing/                    # Cross-cutting test infrastructure
│   ├── simulation/             # Deterministic simulation framework
│   ├── certification/          # 25-test certification harness
│   ├── performance/            # Load generator, latency measurement
│   └── fairness/               # Multi-gateway fairness test suite
│
├── docs/                       # Documentation (specs, diagrams, delivery)
│   ├── specifications/         # 30-file spec baseline
│   ├── diagrams/               # 12 draw.io diagrams + PNG exports
│   ├── delivery/               # Delivery artifacts (decks, plans, packs)
│   └── technical/              # Technical starter artifacts (this cycle)
│
├── .github/                    # CI/CD pipeline definitions
│   └── workflows/
│       ├── build.yml           # Build + unit test
│       ├── integration.yml     # Integration tests
│       └── perf.yml            # Performance regression (daily)
│
├── README.md                   # Project overview and getting-started
├── ARCHITECTURE.md             # Architecture decision records index
├── CONTRIBUTING.md             # Contribution guidelines
└── Makefile / CMakeLists.txt / Cargo.toml / build.gradle
    # Build system — TBD after D-01 (language decision)
```

## Module Ownership and Start Readiness

| Module | Owner Role | Can Start Now? | Blocked By |
|--------|-----------|:--------------:|:----------:|
| `protocol/sbe` | Architect | **Yes** — draft exists | — |
| `protocol/fix` | Architect | **Yes** — draft exists | — |
| `protocol/internal` | Architect | **Yes** — draft exists | — |
| `engine/core` | Engine Lead | No | **D-01** (language) |
| `engine/sequencer` | Engine Lead | No | **D-01, D-04** |
| `engine/journal` | Engine Lead | No | **D-01, D-06** |
| `engine/snapshot` | Engine Lead | No | **D-01** |
| `gateway/fix-gateway` | Gateway Lead | Partial — FIX lib eval in progress | **D-01** for binary parts |
| `gateway/binary-gateway` | Gateway Lead | No | **D-01** |
| `gateway/shared` | Gateway Lead | **Yes** — session state model exists | — |
| `risk/` | Risk Engineer | **Yes** — interface design can start | — |
| `market-data/` | MD Engineer | Partial — encoding can start | Engine output bus interface |
| `reference-data/` | Backend Engineer | **Yes** — data model exists | — |
| `post-trade/` | Backend Engineer | Partial — schema work can start | Fee model decision |
| `surveillance/` | Backend + Compliance | **Yes** — event taxonomy exists | — |
| `ops/console` | Frontend/Full-stack | **Yes** — basic UI work | — |
| `security/` | Security Lead | **Yes** — architecture exists | — |
| `infra/` | SRE | **Yes** — cloud env + monitoring | — |
| `testing/simulation` | QA Lead | **Yes** — framework design exists | — |
| `testing/certification` | QA Lead | **Yes** — test cases exist | — |
| `docs/` | PMO | **Yes** — all content exists | — |

**Summary**: 13 of 21 modules can begin work now. 8 are blocked by D-01 (language).

## Architecture Decision Records (ADR)

Create an `ARCHITECTURE.md` index linking to individual ADR files:

```
docs/adr/
├── ADR-001-single-threaded-per-partition.md       # DECIDED
├── ADR-002-partition-local-sequencing.md           # DECIDED
├── ADR-003-regulatory-grade-durability.md          # DECIDED
├── ADR-004-market-order-residual-policy.md         # DECIDED
├── ADR-005-fairness-at-sequencing-boundary.md      # DECIDED
├── ADR-006-hot-path-language.md                    # OPEN (D-01)
├── ADR-007-journal-implementation.md               # OPEN (D-06)
├── ADR-008-sequencer-strategy.md                   # OPEN (D-04)
├── ADR-009-fix-engine-library.md                   # EVALUATING
└── ADR-010-monitoring-stack.md                     # EVALUATING
```
