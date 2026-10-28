# OSS Readiness Tracker

Status of the `oss-readiness` branch and what is deferred or known-not-working. Items marked *(reported)* come from maintainer notes and were not re-verified when this file was written; confirm before relying on them.

## What this branch did

The branch (13 commits over `main`) prepares the repo for public release. It fixes the build (macOS/clang with OpenSSL 4, GCC `-Wpedantic` via an `__int128_t` typedef, `__int128` intermediates for notional/fee math) and makes OpenSSL a hard CMake requirement, removing the `std::hash` fallback. It adds OSS table-stakes files (LICENSE, CONTRIBUTING, CODE_OF_CONDUCT, SECURITY, `.env.example`) and a minimal GitHub Actions workflow (Ubuntu + Clang). Docs are scrubbed of personal domain, public IP and Hetzner details, and pilot-era participant IDs are renamed to `PARTICIPANT_N`. Functional fixes: the audit log loads its last on-disk hash on open so the chain survives restart; pre-trade risk checks are wired into the FIX and REST order paths; a daemon-wiring batch covers FIX, risk and surveillance; FIX app-message routing is fail-closed, with an enum pinning test.

## Known-not-working on OSS release

- **Iceberg replenishment is only locally tested.** Replenish logic is at `engine/core/src/order_book.cpp:304`; it refreshes `display_qty` to `min(remaining_qty, original_qty)`, not a configured display/peak size. Tests: `engine/core/test/iceberg_test.cpp`. Behaviour under remote/replicated replenishment is undefined (TBD).
- **FOK under STP is not covered.** STP resolution is at `engine/core/src/order_book.cpp:206-265` (FIFO) and `:364-385` (pro-rata). `engine/core/test/stp_modes_test.cpp` has no FOK case. The pro-rata branch handles `CancelNewest` and `CancelBoth` explicitly; check the remaining modes there.
- **Market orders weaken pre-trade risk.** `reference_price` is set to the order price, which is 0 for market orders, so notional checks degrade. Marked `FIXME(risk)` at `daemon/main.cpp:1060` (FIX path) and `ops/src/ops_controller.cpp:1033` (REST path). Fix: feed last-trade or mid from the engine.
- **Market-data snapshot delivery is unimplemented.** `market-data/include/marketdata/publisher.h:116` has `TODO: implement snapshot delivery for gap recovery`; consumers cannot recover from sequence gaps.
- **Portal UI has no automated tests.** `portal/src/` (pages, components, `api.ts`) has no test files and `portal/package.json` has no `test` script. `participant-terminal/` also has no `test` script.
- **CertHarness scenarios fail on Linux** *(reported)*: ValidOrder, RejectedOrder, CrossingTrade, HaltedInstrument, FullCertificationSuite. The acceptor handles one message per TCP connection, but scenarios send `35=D` (`certification/src/cert_harness.cpp:116`) before Logon. Blocked on a FIX stream-framing fix in `gateway/fix/src/tcp/fix_tcp_acceptor.cpp`. Tests: `certification/test/cert_harness_test.cpp`.
- **Pipeline tests SEGFAULT on Linux** *(reported)*: `engine/core/test/pipeline_test.cpp`. Pre-existing; not caused by the OSS hardening commits.
- **Redundant `#ifdef OPENSSL_AVAILABLE` guards** remain in 8 files; OpenSSL is now mandatory, so they can be stripped:
  - `common/test/mtls_test.cpp`
  - `daemon/test/lifecycle_regression_test.cpp`
  - `simulator/src/participant_simulator.cpp`
  - `ops/test/auth_service_test.cpp`
  - `ops/src/auth_service.cpp`, `ops/include/ops/auth_service.h` (comment only)
  - `gateway/fix/test/fix_tls_acceptor_test.cpp`
  - `gateway/fix/src/tcp/fix_tcp_acceptor.cpp`
- **Public-host FIX TLS in Docker** fails C++ TLS init with a Let's Encrypt cert; plain TCP works (`docs/delivery/DEFERRED-ITEMS.md` #1, dated 2026-03-27, so may be stale).
- **Multicast UDP market data** is deferred; TCP/JSON only (`DEFERRED-ITEMS.md` #4).

## Test backfill needed

- `post-trade/test/`: `fee_calculator_test.cpp` and `trade_store_test.cpp` exist; add position-tracker tests and more trade_store edge cases.
- `surveillance/test/`: `rule_unit_tests.cpp` and `surveillance_test.cpp` are smoke-level; add per-rule positive/negative/boundary cases.
- `replication/test/replication_test.cpp:139`: `CrcVerification` fails on macOS *(reported)*; investigate (endianness, alignment or CRC impl).
- `dropcopy/test/drop_copy_test.cpp`: unit only; no end-to-end test (FIX session to drop-copy consumer).
- `ops/test/`: `http_server_test.cpp` and `websocket_server_test.cpp` have limited coverage; add malformed-request, large-body, auth-failure and concurrent-client cases.
- `engine/core/test/`: add FOK x STP and Iceberg x STP cases (see above).
- `portal/`, `participant-terminal/`: no frontend tests. Add Vitest component tests and a Playwright smoke test.
- Market-order risk path: add a test that pins current `reference_price` behaviour before fixing the FIXME.

## Deployment / Operations

- `SECURITY.md` exists with a private-disclosure process (email, 7-day ack, 90-day critical fix target). The address is still the placeholder `security@<your-domain>`; set a real contact before release.
- `.env.example` is present.
- `Dockerfile` is multi-stage: `ubuntu:24.04` C++ builder, `node:22-slim` portal builder, `ubuntu:24.04` runtime. `docker-compose.yml` is at the repo root.
- CI: minimal GitHub Actions workflow (Ubuntu + Clang) only. No macOS or GCC matrix, no sanitizer or coverage jobs.
- No release artifacts; all installs are source builds.
- Repo hygiene before publishing: root contains `pilot-evidence-*` directories, `build/`, `build-mac/`, `Testing/`, and `.cache/clangd` index files tracked in git. Decide what to drop from the public tree.

## Open design questions

- `docs/specifications/27-risks-tradeoffs.md:74`, section 27.3: 20 open stakeholder-workshop questions. Engineering-relevant ones: #4 co-location, #5 hot-path language, #11 FIX versions, #12 DR site, #17 multi-tenant vs single-tenant, #19 position-limit source of truth (engine-internal vs external risk system), #20 implied matching for derivatives.
- `docs/specifications/FINAL-REVIEW-AND-DIAGRAM-PLAN.md:112`: fairness/sequencing boundary waits on a hardware vs software sequencer decision (open question #4 in section 29.3).
- `docs/specifications/FINAL-REVIEW-AND-DIAGRAM-PLAN.md:358`: HA/DR failover sequence, "DR site location TBD"; active-active is a Phase 4 item.
- `docs/specifications/FINAL-REVIEW-AND-DIAGRAM-PLAN.md:361`: rollout/migration swimlane depends on open question #15 (legacy migration).
- `docs/specifications/REVIEW-AND-REVISION-REPORT.md:42`: implementation kickoff "Not ready" pending resolution of section 29.3 open questions. This predates the implementation, so may be stale.
- `docs/delivery/DEFERRED-ITEMS.md`: running register of deferred scope (auto-promote, portal polish, multicast).

## Contributor on-ramps

- **Good first issue:** strip the redundant `#ifdef OPENSSL_AVAILABLE` guards (file list above). Mechanical; the build and tests are the check.
- **Good first issue:** add FOK-under-STP tests to `engine/core/test/stp_modes_test.cpp` and document the expected outcome.
- **Good first issue:** investigate `ReplicationTest.CrcVerification` on macOS (`replication/test/replication_test.cpp:139`).
- **Good first issue:** add Vitest scaffolding and a first test to `portal/` and `participant-terminal/`.
- **Moderate:** replace `reference_price = price` at `daemon/main.cpp:1060` and `ops/src/ops_controller.cpp:1033` with last-trade or mid exposed by the engine.
- **Moderate:** implement market-data snapshot delivery (`market-data/include/marketdata/publisher.h:116`).
- **Moderate:** add per-rule surveillance tests and a drop-copy end-to-end test.
- **Moderate:** extend CI with a macOS job, a GCC job, and an ASan/UBSan job.
- **Hard:** fix FIX acceptor stream framing (multiple messages per connection) in `gateway/fix/src/tcp/fix_tcp_acceptor.cpp`, then re-enable the CertHarness scenarios.
- **Hard:** root-cause the `pipeline_test.cpp` Linux segfault.
- **Hard:** Iceberg peak-size semantics and replicated replenishment behaviour (`order_book.cpp:304`).
