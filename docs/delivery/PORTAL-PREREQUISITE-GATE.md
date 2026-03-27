# Portal Prerequisite Gate

**Date**: 2026-03-26 (updated 2026-03-26)
**Status**: ALL GATES PASSED — Frontend work unblocked
**Purpose**: Enforce strict sequencing before any portal frontend work begins

---

## 1. Portal Prerequisite Gate Summary

Portal frontend work is **blocked** until three sequential gates pass:

```
Gate 1: Phase 2A Complete
         ↓
Gate 2: Backend MVP APIs Built and Tested (8 items)
         ↓
Gate 3: CORS Enabled on HTTP Server
         ↓
UNBLOCKED: Frontend work may begin (read-only Ops Dashboard first)
```

**No gate may be skipped. No frontend work before Gate 3 passes.**

---

## 2. Gate 1: Phase 2A Complete

Phase 2A is complete when ALL of the following are true:

| # | Item | Acceptance Criteria | Status |
|:-:|------|-------------------|:------:|
| 2A-1 | Automatic EOD trigger | EOD fires automatically on session close without manual API call; tested with daemon running through a CLOSED transition | Not started |
| 2A-2 | Top-of-book quantity | Market data `tob` events include non-zero `bid_qty` and `ask_qty` when orders are resting; verified in daemon integration test | Not started |
| 2A-3 | FIX multi-message sessions | A single TCP/TLS connection supports Logon + multiple NewOrderSingle + Cancel + Logout without reconnecting; daemon integration test proves this | Not started |
| 2A-4 | Integration test expansion | At least 10 daemon-level FIX integration tests covering: multi-message session, persistent connection, EOD auto-trigger, TOB qty, and all existing 6 scenarios | Not started |
| 2A-5 | Full test suite passes | All tests (currently 334+) pass including new Phase 2A tests | Not started |
| 2A-6 | Pilot rerun (18 scenarios) | Same simulated external pilot passes with Phase 2A changes active | Not started |

**Gate 1 passes when**: All 6 items are checked, test suite is green, and pilot rerun is 18/18.

**Gate 1 owner**: Engineering lead.

---

## 3. Gate 2: Backend MVP APIs (8 Items)

Each item must be: implemented, tested with at least 2 automated tests, returning proper JSON, and documented with request/response contract.

| # | API | Route | Method | Backend Service | Acceptance Criteria | Status |
|:-:|-----|-------|:------:|----------------|-------------------|:------:|
| B1 | Auth login | `POST /api/v1/auth/login` | POST | New auth service | Accepts username+password or API key; returns JWT with role claim; invalid credentials return 401 | Not started |
| B2 | Auth validate | `GET /api/v1/auth/me` | GET | New auth service | Returns current user identity and role from JWT; expired/invalid token returns 401 | Not started |
| B3 | Kill switch status | `GET /api/v1/kill-switch` | GET | OpsController → KillSwitch | Returns JSON array of blocked participant IDs; empty array when none blocked | Not started |
| B4 | Kill switch activate | `POST /api/v1/kill-switch/{pid}/activate` | POST | OpsController → KillSwitch | Activates kill switch; returns confirmation JSON; requires Operator role; audit log entry written | Not started |
| B5 | Kill switch deactivate | `POST /api/v1/kill-switch/{pid}/deactivate` | POST | OpsController → KillSwitch | Deactivates; returns confirmation; requires Operator role; audit log entry written | Not started |
| B6 | FIX session list | `GET /api/v1/fix/sessions` | GET | OpsController → FixTcpAcceptor | Returns JSON array of sessions with: session_id, sender_comp_id, logged_in, inbound_seq, outbound_seq | Not started |
| B7 | Ref-data write (instruments) | `POST /api/v1/instruments` | POST | RefDataHttp → RefDataService | Creates instrument from JSON body; validates required fields; returns created instrument; requires Admin role; audit log entry written | Not started |
| B8 | Ref-data write (participants) | `POST /api/v1/participants` | POST | RefDataHttp → RefDataService | Creates participant from JSON body; validates required fields; returns created participant; requires Admin role; audit log entry written | Not started |

### Supporting items (must ship alongside the 8 APIs):

| # | Item | Acceptance Criteria | Status |
|:-:|------|-------------------|:------:|
| B9 | Audit log (append-only) | Every B4, B5, B7, B8 call writes a JSON-line audit record with timestamp, user, action, target, details; file-based, no DB required | Not started |
| B10 | Risk limits read | `GET /api/v1/participants/{id}/risk-limits` returns current limits JSON | Not started |
| B11 | EOD last result | `GET /api/v1/eod/status` returns last EOD trigger result or "no EOD run" | Not started |

**Gate 2 passes when**: All 11 items (B1–B11) are implemented, tested, and returning correct JSON. Auth service issues valid JWTs. Audit log writes verified. Full test suite still passes.

**Gate 2 owner**: Engineering lead.

---

## 4. Gate 3: CORS Enabled

| # | Item | Acceptance Criteria | Status |
|:-:|------|-------------------|:------:|
| G3-1 | HTTP server returns `Access-Control-Allow-Origin` header | Configurable origin (or `*` for dev); header present on all responses | Not started |
| G3-2 | HTTP server handles OPTIONS preflight | Returns 200 with CORS headers for preflight requests; no auth required on OPTIONS | Not started |
| G3-3 | `Access-Control-Allow-Headers` includes `Authorization` | JWT Bearer token header passes preflight | Not started |
| G3-4 | Verified from browser | `fetch()` from `localhost:3000` (or equivalent) to `localhost:8180` succeeds without CORS error | Not started |

**Gate 3 passes when**: All 4 items checked. A browser-based `fetch()` to the API succeeds.

**Gate 3 owner**: Engineering.

---

## 5. Do Not Start Frontend Until

**This is the explicit block list. No exceptions.**

| # | Condition | Verified By |
|:-:|-----------|:-----------:|
| 1 | Gate 1 (Phase 2A) is complete and signed off | Engineering lead |
| 2 | Gate 2 (all 11 backend items) is complete and tested | Engineering lead |
| 3 | Gate 3 (CORS) is complete and browser-verified | Engineering |
| 4 | API contracts documented (request/response JSON for each endpoint) | Engineering |
| 5 | Auth service issues valid JWTs with role claims | Engineering |
| 6 | Audit log writes verified for all mutating endpoints | Engineering |
| 7 | Full test suite passes (all existing + new tests) | CI or manual |

**If any condition is not met, frontend work is blocked.**

**The first permitted frontend task**: read-only Ops Dashboard consuming only existing APIs (health, instruments, metrics). No write operations. No admin surfaces. No auth UI beyond login.

---

## 6. Recommended Next Execution Step

**Start Phase 2A.**

The portal gate cannot be opened until Phase 2A is done. Phase 2A is the current blocker for all downstream work including portal, broader pilot, and production path.

Phase 2A scope (from the approved Phase 2 Decision Pack):

| # | Item | Effort |
|:-:|------|:------:|
| 1 | Automatic EOD trigger on session close | S |
| 2 | Top-of-book quantity tracking in OrderBook | S |
| 3 | FIX persistent multi-message sessions | M |
| 4 | Integration test expansion | S |

**Estimated Phase 2A duration**: 2-4 weeks.

**After Phase 2A**: Gate 2 backend items can begin (3-4 weeks). Then Gate 3 (1-2 days). Then frontend.

**Total time to portal frontend unblock**: approximately 6-8 weeks from now.

---

## Gate Status Tracker

| Gate | Items | Passed | Status |
|------|:-----:|:------:|:------:|
| Gate 1: Phase 2A | 6 | 6/6 | **PASSED** (340 tests, multi-msg FIX, auto EOD, TOB qty) |
| Gate 2: Backend APIs | 11 | 11/11 | **PASSED** (370 tests, auth, audit, kill switch, FIX sessions, refdata write, risk read, EOD status) |
| Gate 3: CORS | 4 | 4/4 | **PASSED** (OPTIONS preflight, CORS headers on all responses, Authorization header allowed) |
| **Frontend unblocked** | — | — | **UNBLOCKED — portal frontend work may begin** |

**All gates passed.** First permitted frontend task: read-only Ops Dashboard.
