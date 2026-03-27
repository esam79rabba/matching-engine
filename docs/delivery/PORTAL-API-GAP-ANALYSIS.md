# Portal Backend API Gap Analysis

**Date**: 2026-03-26
**Source of truth**: Portal Concept Pack + current codebase
**Purpose**: Identify every backend API gap before portal build can begin

---

## Consolidated API Gap Table

### Ops Portal APIs

| # | Portal Function | HTTP Route | Backend Service | Status | Gap Type | Auth | Audit | Portal Phase |
|:-:|----------------|-----------|----------------|:------:|:--------:|:----:|:-----:|:------------:|
| O1 | Health overview | `GET /api/v1/health` | OpsController | **Exists** | — | Open | No | Ops MVP |
| O2 | Instrument list + state | `GET /api/v1/instruments` | OpsController + RefDataHttp | **Exists** | — | Open | No | Ops MVP |
| O3a | Instrument detail | `GET /api/v1/instruments/{id}` | OpsController | **Exists** | — | Open | No | Ops MVP |
| O3b | Halt instrument | `POST /api/v1/instruments/{id}/halt` | OpsController | **Exists** | — | API key | No | Ops MVP |
| O3c | Resume instrument | `POST /api/v1/instruments/{id}/resume` | OpsController | **Exists** | — | API key | No | Ops MVP |
| O4a | Kill switch activate | `POST /api/v1/kill-switch/{pid}/activate` | KillSwitch (engine) | **Missing** | Thin wrapper | API key | **Needed** | Ops MVP |
| O4b | Kill switch deactivate | `POST /api/v1/kill-switch/{pid}/deactivate` | KillSwitch (engine) | **Missing** | Thin wrapper | API key | **Needed** | Ops MVP |
| O4c | Kill switch status | `GET /api/v1/kill-switch` | KillSwitch (engine) | **Missing** | Thin wrapper | Open | No | Ops MVP |
| O5a | FIX session list | `GET /api/v1/fix/sessions` | FixTcpAcceptor | **Missing** | Thin wrapper | Open | No | Ops MVP |
| O5b | FIX session detail | `GET /api/v1/fix/sessions/{id}` | FixTcpAcceptor | **Missing** | Thin wrapper | Open | No | Ops MVP |
| O5c | FIX session disconnect | `POST /api/v1/fix/sessions/{id}/disconnect` | FixTcpAcceptor | **Missing** | Thin wrapper | API key | **Needed** | Later |
| O6a | Metrics (Prometheus) | `GET /api/v1/metrics` | Metrics singleton | **Exists** | — | Open | No | Ops MVP |
| O6b | Metrics (JSON) | `GET /api/v1/metrics/json` | Metrics singleton | **Exists** | — | Open | No | Ops MVP |
| O7a | EOD trigger | `POST /api/v1/eod/trigger` | OpsController + EodProcessor | **Exists** | — | API key | No | Ops MVP |
| O7b | EOD last result | `GET /api/v1/eod/status` | EodProcessor | **Missing** | Moderate | Open | No | Ops MVP |
| O8 | Session schedule | `GET /api/v1/scheduler/status` | SessionScheduler | **Missing** | Moderate | Open | No | Later |
| O9 | Event timeline | `GET /api/v1/events?limit=N` | **Not built** | **Missing** | Substantial | Open | No | Later |
| O10 | Alert config | `GET/POST /api/v1/alerts` | **Not built** | **Missing** | Substantial | API key | No | Later |

### Admin Portal APIs

| # | Portal Function | HTTP Route | Backend Service | Status | Gap Type | Auth | Audit | Portal Phase |
|:-:|----------------|-----------|----------------|:------:|:--------:|:----:|:-----:|:------------:|
| C1 | Instrument list (read) | `GET /api/v1/instruments` | RefDataHttp | **Exists** | — | Open | No | Admin MVP |
| C2a | Create instrument | `POST /api/v1/instruments` | RefDataService | **Missing** | Moderate | Admin | **Needed** | Admin MVP |
| C2b | Update instrument | `PUT /api/v1/instruments/{id}` | RefDataService | **Missing** | Moderate | Admin | **Needed** | Admin MVP |
| C2c | Delete/delist instrument | `DELETE /api/v1/instruments/{id}` | RefDataService + Engine | **Missing** | Moderate | Admin | **Needed** | Later |
| C3 | Participant list (read) | `GET /api/v1/participants` | RefDataHttp | **Exists** | — | Open | No | Admin MVP |
| C4a | Create participant | `POST /api/v1/participants` | RefDataService | **Missing** | Moderate | Admin | **Needed** | Admin MVP |
| C4b | Update participant | `PUT /api/v1/participants/{id}` | RefDataService | **Missing** | Moderate | Admin | **Needed** | Admin MVP |
| C5 | Session schedule (read) | `GET /api/v1/sessions` | RefDataHttp | **Exists** (basic) | — | Open | No | Later |
| C6 | Session schedule (write) | `PUT /api/v1/sessions/{id}` | SessionScheduler | **Missing** | Substantial | Admin | **Needed** | Later |
| C7a | Risk limits (read) | `GET /api/v1/participants/{id}/risk-limits` | RiskService | **Missing** | Thin wrapper | Open | No | Admin MVP |
| C7b | Risk limits (write) | `PUT /api/v1/participants/{id}/risk-limits` | RiskService | **Missing** | Moderate | Admin | **Needed** | Later |
| C8 | Instrument listing/delist | `POST /api/v1/instruments/{id}/activate\|delist` | InstrumentLifecycle | **Missing** | Thin wrapper | Admin | **Needed** | Later |
| C9 | Audit log (read) | `GET /api/v1/audit?limit=N` | **Not built** | **Missing** | Substantial | Admin | Self | Later |
| C10 | Config approval | `POST /api/v1/approvals` | **Not built** | **Missing** | Substantial | Admin | **Needed** | Later |

### Shared / Auth APIs

| # | Portal Function | HTTP Route | Backend Service | Status | Gap Type | Portal Phase |
|:-:|----------------|-----------|----------------|:------:|:--------:|:------------:|
| S1 | User login | `POST /api/v1/auth/login` | **Not built** | **Missing** | Substantial | MVP |
| S2 | Token validation | `GET /api/v1/auth/validate` | **Not built** | **Missing** | Substantial | MVP |
| S3 | Current user info | `GET /api/v1/auth/me` | **Not built** | **Missing** | Moderate | MVP |
| S4 | User CRUD | `GET/POST/PUT /api/v1/auth/users` | **Not built** | **Missing** | Substantial | Later |

---

## Gap Summary

| Category | Existing | Thin Wrapper | Moderate | Substantial | Total Gaps |
|----------|:--------:|:------------:|:--------:|:-----------:|:----------:|
| Ops Portal | 8 | 4 | 2 | 2 | 8 |
| Admin Portal | 3 | 2 | 5 | 3 | 10 |
| Auth/Shared | 0 | 0 | 1 | 3 | 4 |
| **Total** | **11** | **6** | **8** | **8** | **22** |

---

## Data Contracts Needed

### Thin Wrappers (expose existing internal state as JSON)

| API | Request | Response Contract |
|-----|---------|-------------------|
| Kill switch status | `GET` | `{"participants": [{"id": 1, "blocked": true}]}` |
| Kill switch activate | `POST {"participant_id": N}` | `{"action": "activated", "participant_id": N}` |
| Kill switch deactivate | `POST {"participant_id": N}` | `{"action": "deactivated", "participant_id": N}` |
| FIX session list | `GET` | `{"sessions": [{"id": N, "comp_id": "X", "logged_in": true, "inbound_seq": N, "outbound_seq": N}]}` |
| FIX session detail | `GET` | Same as list item, single session |
| Risk limits read | `GET` | `{"participant_id": N, "max_order_qty": N, "max_notional": N, "max_price_deviation_pct": N}` |

### Moderate Work (add write paths to existing services)

| API | Request Contract | Backend Change |
|-----|-----------------|----------------|
| Create instrument | `POST {"instrument_id": N, "symbol": "X", "tick_size": N, ...}` | Add HTTP write handler to RefDataHttp; call `ref_data.add_instrument()` + register in partition |
| Update instrument | `PUT {"tick_size": N, "static_band_pct": N, ...}` | Add HTTP write handler; call `ref_data.update_instrument()` + propagate to engine |
| Create participant | `POST {"participant_id": N, "name": "X", "sender_comp_id": "X", "risk_limits": {...}}` | Add HTTP write handler; call `ref_data.add_participant()` |
| Update participant | `PUT {"name": "X", "risk_limits": {...}}` | Add HTTP write handler |
| Risk limits write | `PUT {"max_order_qty": N, ...}` | Add HTTP handler; call `risk_svc.set_participant_limits()` — **method exists but not exposed** |
| EOD last result | `GET` | Store last EOD result in OpsController; return on request |
| User info | `GET` | Extract from auth token |

### Substantial New Work

| API | What Needs Building |
|-----|-------------------|
| Auth login/validate | JWT generation, user store (file or DB), password/key verification |
| User CRUD | User model, storage, role assignment |
| Event timeline | In-memory event ring buffer, structured event types, query API |
| Audit log | Audit record model, append-only store, query API |
| Session schedule write | Scheduler mutation API, validation, persistence |
| Alert config | Alert rule model, threshold evaluation loop, notification |
| Config approval | Approval workflow state machine, multi-user dependency |

---

## Auth/Role Implications Per API

| API | Current Auth | Portal Requires |
|-----|:------------:|:---------------:|
| Health, instruments (read), metrics | Open | Open (no change) |
| Halt, resume, EOD trigger | API key | Operator role |
| Kill switch | **No HTTP API yet** | Operator role + confirmation |
| FIX session disconnect | **No HTTP API yet** | Operator role + confirmation |
| Instrument create/edit | **No HTTP API yet** | Admin role + audit |
| Participant create/edit | **No HTTP API yet** | Admin role + audit |
| Risk limits write | **No HTTP API yet** | Admin role + audit |
| User management | **Not built** | Super Admin role |

---

## Audit/Logging Implications

Actions requiring audit trail before portal deployment:

| Action | Why | Implementation |
|--------|-----|---------------|
| Kill switch activate/deactivate | Safety-critical; regulatory evidence | Append to audit log with user + timestamp |
| Instrument halt/resume | Market impact | Append to audit log |
| Instrument create/edit/delist | Config integrity | Append to audit log |
| Participant create/edit | Access control change | Append to audit log |
| Risk limit changes | Risk posture change | Append to audit log |
| FIX session disconnect | Participant impact | Append to audit log |

**Minimum audit implementation**: append-only file with JSON lines (`{"timestamp": "...", "user": "...", "action": "...", "target": "...", "details": {...}}`). No database required for MVP.

---

## Recommended MVP Cut Line

### Portal MVP Backend (must exist before any UI work)

| # | API | Type | Effort | Blocks |
|:-:|-----|:----:|:------:|--------|
| 1 | Kill switch HTTP wrapper (status/activate/deactivate) | Thin | S | Ops MVP |
| 2 | FIX session list API | Thin | S | Ops MVP |
| 3 | Risk limits read API | Thin | S | Admin MVP |
| 4 | Instrument create/update API | Moderate | M | Admin MVP |
| 5 | Participant create/update API | Moderate | M | Admin MVP |
| 6 | EOD last result API | Moderate | S | Ops MVP |
| 7 | Auth service (login + JWT + 2 roles) | Substantial | M | Both MVPs |
| 8 | Audit log (append-only file) | Moderate | S | Admin MVP |

**Estimated total backend MVP effort**: 3-4 weeks for one backend engineer.

### Below the Cut Line (build after portal MVP ships)

- FIX session disconnect API
- Risk limits write API
- Session schedule read/write API
- Instrument delist API
- Event timeline API
- Alert configuration
- Config approval workflow
- User CRUD / role management
- Audit log query UI

---

## Backend Prerequisites Before Portal Build

| # | Prerequisite | Why | Status |
|:-:|-------------|-----|:------:|
| 1 | Phase 2A complete | APIs must be stable; FIX multi-message changes acceptor interface | Not started |
| 2 | 8 MVP backend APIs built and tested | Portal is useless without them | Not started |
| 3 | Auth service operational | Portal without auth is a regression | Not started |
| 4 | Audit log mechanism in place | Mutating admin actions without audit are unacceptable | Not started |
| 5 | CORS headers on HTTP server | Browser-based portal needs CORS | Not implemented |
| 6 | JSON content-type consistency | All APIs must return proper JSON | Mostly done |

---

## Top 5 Backend Items to Build First (If Portal Approved)

| Priority | Item | Why First | Effort | Unblocks |
|:--------:|------|-----------|:------:|----------|
| 1 | **Auth service (JWT + 2 roles)** | Every portal page depends on it; must be designed before any UI | M | All portal pages |
| 2 | **Kill switch HTTP wrapper** | Highest-value ops control not yet exposed; thin wrapper on existing engine capability | S | Ops MVP kill switch page |
| 3 | **FIX session list API** | Operators need session visibility; thin wrapper on existing acceptor state | S | Ops MVP sessions page |
| 4 | **Ref-data write APIs (instrument + participant)** | Admin portal's core value; moderate work on existing service | M | Admin MVP instrument + participant pages |
| 5 | **Audit log (append-only)** | Must exist before any admin write action is exposed via UI | S | Admin MVP (all write pages) |

**After these 5**: the remaining 3 MVP items (risk limits read, EOD status, CORS) are small additions that can be done in parallel with early UI work.
