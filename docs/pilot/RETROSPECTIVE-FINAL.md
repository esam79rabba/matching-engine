# Pilot Retrospective

**Date**: 2026-03-26
**Facilitator**: Esam Rabba
**Scope**: Self-pilot (2026-03-25) + external pilot (2026-03-26)

---

## What Worked Well

1. **Core matching engine is solid** — 326 tests, deterministic matching, price-time FIFO correct, crossing trades execute properly. No engine bugs found during pilot.

2. **Rapid blocker resolution** — 2 P1 issues (OrderID mismatch, string ClOrdID) found and fixed in the same session. The codebase was clean enough to diagnose quickly.

3. **TLS integration was smooth** — OpenSSL wired through transport abstraction. TLS v1.3 handshake + encrypted FIX session worked first try.

4. **Config-driven startup works** — JSON instruments, sessions, platform.conf all loaded correctly. No hardcoded assumptions remained.

5. **Pilot documentation was usable** — execution pack, dry-run guide, evidence collection, and checklists were used in real operation and proved effective.

6. **Metrics endpoint immediately useful** — Prometheus counters confirmed exact message/order/cancel counts during pilot.

## What Failed

1. **Cancel was broken in daemon** — The daemon used `partition.sequence()` instead of the composite OrderId. This was a wiring bug, not an engine bug. Root cause: the daemon was written without an integration test against the cancel flow.

2. **String ClOrdIDs failed silently** — `std::stoull()` threw an exception that was caught and swallowed, returning nullopt. No error response was sent to the client. Silent drops are unacceptable.

3. **Auth was not wired** — The auth middleware existed and was tested, but nobody wired it into the daemon startup. It was dead code until the hardening pass.

## What Needed Rework

1. **Daemon FIX response logic** — The initial daemon callback was too simplistic. It needed explicit handling for Filled (with synthesized fill when fills vector empty), cancel errors, and parse failures.

2. **Market data listener** — Only published from the fills vector (which is empty). Had to add trade synthesis and top-of-book publishing.

3. **TLS config in daemon** — TLS was available via the transport layer but not readable from the platform config. Required a small wiring addition.

## What Surprised Us

1. **The fills vector being empty** — The engine correctly sets `action=Filled` but doesn't populate the `fills` vector. This was not obvious from the test suite because tests check `action` not `fills`.

2. **Port 8080 conflict** — A Java process was already on 8080. This is a deployment environment issue, not a platform issue, but the daemon didn't detect or report it clearly.

3. **How fast blocker fixes went** — Both P1 fixes were targeted, small, and didn't require engine redesign. The architecture held up well under real-world testing.

## Hardening Before Wider Rollout

| # | Item | Effort | Priority | Rationale |
|:-:|------|:------:|:--------:|-----------|
| 1 | Populate fills vector in matching engine | S | P2 | Remove synthesized-fill workaround |
| 2 | Wire EOD into daemon | M | P2 | Operators need EOD files without test framework |
| 3 | Add integration test for full daemon FIX flow | S | P1 | Would have caught OrderID and ClOrdID bugs |
| 4 | Port conflict detection at startup | S | P3 | Better error message when port occupied |
| 5 | Log all FIX processing decisions | S | P2 | Silent drops were the worst part of debugging |

## What Should NOT Be Built Yet

| Item | Why Defer |
|------|-----------|
| Full RBAC | API key auth is sufficient for controlled pilot scope |
| mTLS enforcement | Server TLS is sufficient; client certs add complexity without pilot demand |
| Multicast market data | No participant has requested it; TCP/JSON works for pilot scale |
| Auction mechanism | No regulatory requirement for FR-1 pilot scope |
| DR/failover | Single-site warm restart is acceptable for pilot; DR is Phase 2 infrastructure |
| Container orchestration | Systemd service unit is sufficient for single-host pilot |

## Phase 2 Focus Recommendation

Based on pilot evidence, Phase 2 should prioritize:

1. **Production journal** (NVMe Direct I/O) — current segmented journal works but isn't production-grade
2. **Fills vector fix** — remove the workaround, make the engine populate fills properly
3. **EOD daemon integration** — operators need this for daily reconciliation
4. **Full daemon integration test** — the biggest gap exposed by the pilot
5. **DR/replication** — required for any venue beyond controlled pilot
