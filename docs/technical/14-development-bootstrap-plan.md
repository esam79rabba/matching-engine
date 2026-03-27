# Development Bootstrap Plan — First 2 Weeks

**Status**: Execution-ready once D-01 (language) closes
**Assumption**: Monorepo layout per §11 (repository structure draft)
**Note**: Tasks marked [NOW] can start before D-01 closes

---

## Week 0 (Pre-Decision: Start Immediately)

| Day | Task | Owner | Blocked By |
|-----|------|-------|:----------:|
| 0 | Create monorepo skeleton (empty module directories per structure draft) | Engineering Lead | — [NOW] |
| 0 | Set up Git repository with branch protection (main, develop) | Engineering Lead | — [NOW] |
| 0 | Create README.md with project overview and getting-started | PMO | — [NOW] |
| 0 | Create ARCHITECTURE.md with ADR index (frozen + open decisions) | Architect | — [NOW] |
| 0 | Create CONTRIBUTING.md with coding standards placeholder | Engineering Lead | — [NOW] |
| 1 | Set up CI pipeline skeleton (build + lint + test stages, empty jobs) | SRE | — [NOW] |
| 1 | Copy protocol drafts (SBE, FIX, events) into `protocol/` module | Architect | — [NOW] |
| 1 | Copy spec baseline and diagrams into `docs/` module | PMO | — [NOW] |
| 2 | Set up cloud development environment (VMs, networking) | SRE | — [NOW] |
| 2 | Configure monitoring stack (Prometheus + Grafana) in dev env | SRE | — [NOW] |
| 3 | Draft coding standards document (style, naming, error handling) | Engineering Lead | Partial — language-agnostic sections [NOW]; language-specific after D-01 |
| 3 | Set up Slack/Teams channels for each workstream | Program Manager | — [NOW] |
| 4 | Set up issue tracker (Jira/Linear) with workstream boards | Program Manager | — [NOW] |
| 4 | Create ADR-001 through ADR-005 (frozen decisions) from baseline | Architect | — [NOW] |

## Week 1 (Post D-01: Language Known)

| Day | Task | Owner | Depends On |
|-----|------|-------|:----------:|
| 5 | Configure build system (CMake / Cargo / Gradle) for monorepo | Engineering Lead | D-01 |
| 5 | Create engine/core module with empty build target | Engine Lead | D-01 |
| 5 | Create gateway/fix-gateway module with FIX library dependency | Gateway Lead | D-01 + FIX lib eval |
| 5 | Set up CI build job (compile + run empty tests) | SRE | D-01 |
| 6 | Implement Order struct/class per order-book design draft | Engine Lead | D-01 |
| 6 | Implement PriceLevel struct/class | Engine Lead | D-01 |
| 6 | Implement OrderPool (pre-allocated, free-list) | Engine Lead | D-01 |
| 7 | Implement OrderBook skeleton (insert, cancel, best bid/ask) | Engine Lead | Day 6 |
| 7 | Set up unit test framework and first test (insert + cancel) | QA Lead | D-01 |
| 8 | Implement price-time FIFO matching (single price level) | Engine Lead | Day 7 |
| 8 | Add matching unit tests (basic match, partial fill, no match) | QA Lead | Day 8 |
| 9 | Implement multi-level matching (sweep through levels) | Engine Lead | Day 8 |
| 9 | Add STP check hook (cancel-newest mode) | Engine Lead | Day 8 |

## Week 2 (Post D-01: Core Taking Shape)

| Day | Task | Owner | Depends On |
|-----|------|-------|:----------:|
| 10 | Implement instrument state machine (PRE_OPEN → CONTINUOUS → CLOSED) | Engine Lead | Day 7 |
| 10 | Implement price-band validation | Engine Lead | Day 7 |
| 10 | Implement tick-size validation | Engine Lead | Day 7 |
| 11 | Implement market-order residual policy (cancel default + collar option) | Engine Lead | Day 9 |
| 11 | Add IOC and FOK handling | Engine Lead | Day 9 |
| 12 | Implement journal write interface (abstract — impl after D-06) | Engine Lead | Day 6 |
| 12 | Implement snapshot capture (serialize book state) | Engine Lead | Day 7 |
| 12 | First deterministic replay test (record input → replay → compare state) | QA Lead | Day 12 |
| 13 | FIX gateway: Logon/Logout/Heartbeat session handling | Gateway Lead | FIX lib integrated |
| 13 | FIX gateway: NewOrderSingle decode → internal format | Gateway Lead | Day 13 |
| 14 | First end-to-end test: FIX submit → engine match → execution report | Integration | Days 9 + 13 |
| 14 | **MILESTONE 1 CHECKPOINT** | Engineering Lead | Day 14 |

## Coding Standards (Language-Agnostic Sections)

These apply regardless of D-01:

| Standard | Rule |
|----------|------|
| **Naming** | snake_case for functions/variables; PascalCase for types/classes; UPPER_SNAKE for constants |
| **Files** | Max 800 lines; one primary type per file; tests in parallel directory |
| **Functions** | Max 50 lines; single responsibility; no deep nesting (> 4 levels) |
| **Error handling** | Explicit at every level; no silent swallowing; structured error codes |
| **Immutability** | Prefer immutable data; new objects over mutation where practical |
| **No allocation on hot path** | Pre-allocate all pools at startup; zero malloc/new in matching loop |
| **No I/O on hot path** | No logging, no network calls, no disk reads in matching loop (journal write only) |
| **Comments** | Only where logic is non-obvious; no redundant comments restating code |
| **Commits** | Conventional commits: feat/fix/refactor/test/docs; one logical change per commit |
| **Reviews** | All changes reviewed before merge; engine/core requires 2 reviewers |

## ADR Approach

Architecture Decision Records follow this template:

```markdown
# ADR-NNN: [Title]

**Status**: DECIDED | OPEN | EVALUATING
**Date**: YYYY-MM-DD
**Deciders**: [Names/Roles]

## Context
[Why this decision is needed]

## Decision
[What was decided]

## Consequences
[What changes as a result]

## Alternatives Considered
[What else was evaluated and why it was not chosen]
```

## Environment Separation

| Environment | Purpose | Infrastructure | Data |
|-------------|---------|---------------|------|
| **Local dev** | Developer workstation | Docker or native build | Synthetic |
| **CI** | Automated build + test | Cloud VMs (ephemeral) | Synthetic |
| **Dev/Integration** | Shared integration testing | Cloud VMs (persistent) | Synthetic |
| **Certification** | Participant testing | Dedicated (mirrors prod config) | Synthetic |
| **Production** | Live trading | Bare-metal (per deployment profile) | Real |

## Stub/Mock Strategy for Unresolved Dependencies

| Dependency | Stub Approach |
|-----------|---------------|
| Journal (D-06 open) | In-memory journal (ArrayList/Vec) implementing journal interface; swap to real impl after D-06 |
| Sequencer (D-04 open) | Simple monotonic counter; swap to PTP-timestamped or HW sequencer after D-04 |
| Reference data service | Static JSON/YAML config files loaded at startup; swap to API-backed service later |
| Risk service | Pass-through stub (all orders pass); swap to real risk checks incrementally |
| Market data publisher | Console/log output; swap to multicast publisher later |
| Post-trade | Log trades to file; swap to real capture service later |

**Principle**: Every external dependency has an interface/trait/abstract class. Stubs implement the same interface. Swap is a configuration change, not a code change.
