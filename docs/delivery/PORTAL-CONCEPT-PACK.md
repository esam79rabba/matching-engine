# Phase 2B Portal Concept Pack

**Date**: 2026-03-26
**Status**: Concept — not approved for implementation yet
**Prerequisite**: Phase 2A complete (auto EOD, TOB qty, FIX multi-message, test expansion)
**Target phase**: Phase 2B or 2C depending on stakeholder priority

---

## 1. Portal Strategy Summary

### Why a Portal Is Useful

The current platform is operated entirely via CLI (`curl`), scripts, and daemon logs. This works for engineering pilots but is not sustainable for:
- Operations staff who need visual situational awareness
- Compliance teams who need audit visibility without CLI access
- Participant onboarding that currently requires manual JSON file editing
- Runtime configuration changes that currently require daemon restart

### Why It Should NOT Be Phase 2A

Phase 2A priorities (auto EOD, TOB qty, FIX multi-message) are direct pilot blockers. A portal adds no value until those are resolved and the backend APIs are stable. Building UI before backend contracts are settled creates throwaway work.

### Why Ops and Admin Should Be Separated

| Concern | Ops Portal | Admin Portal |
|---------|-----------|-------------|
| User | Operator on shift | Platform administrator |
| Frequency | Continuous during trading | Infrequent (pre-session, onboarding) |
| Risk profile | Real-time control actions (halt, kill switch) | Configuration changes (instruments, participants) |
| Audit urgency | Immediate (incident response) | Pre-approved (change control) |
| Downtime tolerance | Zero — must always be available | Can be offline during trading |

Mixing these creates confusion about who can do what and when.

### What Should Stay Outside the Portal

- Matching engine internals (order book visualization at the order level)
- Journal/snapshot file management (infrastructure concern)
- TLS certificate management (security tooling, not UI)
- Daemon process management (systemd, not UI)
- Source code or build operations

---

## 2. Ops Portal Concept

| # | Capability | Purpose | User | Backend Dependency | MVP |
|:-:|-----------|---------|------|-------------------|:---:|
| O1 | Platform health overview | At-a-glance: daemon up, partitions active, instruments count | Operator | `GET /api/v1/health` (exists) | Yes |
| O2 | Instrument state dashboard | All instruments with state (Continuous/Halted/Closed), best bid/ask, order count | Operator | `GET /api/v1/instruments` (exists) | Yes |
| O3 | Halt / Resume controls | Button per instrument to halt or resume | Operator | `POST /api/v1/instruments/{id}/halt\|resume` (exists, auth-protected) | Yes |
| O4 | Kill switch panel | Activate/deactivate per participant | Operator | Kill switch API (exists in engine, needs HTTP wrapper) | Yes |
| O5 | FIX session state | Active sessions, CompIDs, sequence numbers, logged-in status | Operator | **Not exposed via API — needs new endpoint** | Yes |
| O6 | Metrics summary | Key counters: orders, trades, rejects, sessions, cancels | Operator | `GET /api/v1/metrics` (exists, Prometheus format) | Yes |
| O7 | EOD trigger / status | Trigger EOD, show last EOD result | Operator | `POST /api/v1/eod/trigger` (exists) | Yes |
| O8 | Session schedule view | Current session phase, next transition time | Operator | **Not exposed via API — needs new endpoint** | Later |
| O9 | Event timeline | Recent FIX events (logon, trade, halt, error) from structured logs | Operator | **Not exposed — needs log streaming or event buffer API** | Later |
| O10 | Alert configuration | Define alert thresholds on metrics | Operator | **Not built — needs alerting subsystem** | Later |

---

## 3. Admin / Configuration Portal Concept

| # | Capability | Purpose | User | Backend Dependency | MVP |
|:-:|-----------|---------|------|-------------------|:---:|
| C1 | Instrument list / detail | View all instruments with config (tick, lot, band, ref price) | Admin | `GET /api/v1/instruments` (exists, read-only) | Yes |
| C2 | Instrument create / edit | Add new instrument, modify tick/lot/band | Admin | **Not exposed — needs write API on ref-data service** | Yes |
| C3 | Participant list / detail | View participants with CompID, risk limits | Admin | `GET /api/v1/participants` (exists via ref-data HTTP) | Yes |
| C4 | Participant create / edit | Onboard new participant, set risk limits | Admin | **Not exposed — needs write API on ref-data service** | Yes |
| C5 | Session profile view | View session schedule (PRE_OPEN → CONTINUOUS → CLOSED times) | Admin | **Not exposed via API** | Later |
| C6 | Session profile edit | Modify session times | Admin | **Not exposed — needs write API** | Later |
| C7 | Risk limit configuration | View/edit per-participant fat-finger, rate limit, notional | Admin | **Not exposed — needs risk config API** | MVP (read), Later (write) |
| C8 | Instrument listing/delisting | Activate or delist an instrument at runtime | Admin | Instrument lifecycle (exists in engine) | Later |
| C9 | Config change audit trail | Log of who changed what and when | Admin/Compliance | **Not built — needs audit log subsystem** | Later |
| C10 | Config approval workflow | Require second approval for high-risk changes | Admin | **Not built** | Later |

---

## 4. Shared UI / Platform Layer

| Component | Purpose | Notes |
|-----------|---------|-------|
| Auth shell | Login, session management, role display | Wraps backend auth; initially API key, later RBAC |
| Navigation frame | Sidebar with Ops / Admin sections | Role-based visibility |
| Environment banner | "PILOT" / "STAGING" / "PRODUCTION" indicator | Prevents accidental production actions |
| Toast / notification system | Success/error feedback on actions | Standard UI pattern |
| Confirmation dialogs | "Are you sure?" for halt, kill switch, delist | Required for destructive actions |
| Audit log hook | Every mutating action logged with user, timestamp, action | Shared service call |
| Data table component | Sortable, filterable tables for instruments, participants, sessions | Reusable |
| Status badge component | Green/amber/red for instrument state, session state, health | Reusable |
| Auto-refresh | Periodic poll for dashboard data (5-10 second interval) | Not WebSocket for MVP |

---

## 5. API Dependency Map

| Portal Function | Backend API | Status |
|----------------|------------|:------:|
| O1: Health | `GET /api/v1/health` | Exists |
| O2: Instrument state | `GET /api/v1/instruments` | Exists |
| O3: Halt/resume | `POST /api/v1/instruments/{id}/halt\|resume` | Exists |
| O4: Kill switch | Kill switch in engine | **Needs HTTP wrapper** |
| O5: FIX sessions | Session state in acceptor | **Needs new API** |
| O6: Metrics | `GET /api/v1/metrics` | Exists (Prometheus) |
| O7: EOD trigger | `POST /api/v1/eod/trigger` | Exists |
| O8: Session schedule | Scheduler state | **Needs new API** |
| O9: Event timeline | Structured logs | **Needs event buffer API** |
| C1: Instrument read | `GET /api/v1/instruments` | Exists |
| C2: Instrument write | Ref-data mutation | **Needs write API** |
| C3: Participant read | `GET /api/v1/participants` | Exists |
| C4: Participant write | Ref-data mutation | **Needs write API** |
| C7: Risk limits read | Risk service config | **Needs read API** |
| C7: Risk limits write | Risk service config | **Needs write API** |

**Summary**: 7 existing APIs, 7 new APIs needed for MVP portal.

---

## 6. Security / Access Model

### Current State

- Single API key for all ops actions
- No user identity
- No role separation
- No audit trail of who performed actions

### Target Portal Security Model

| Layer | Current | Target |
|-------|---------|--------|
| Authentication | API key | User login (JWT or session-based) |
| Authorization | All-or-nothing | Role-based (Operator, Admin, Viewer) |
| Audit | None | Every mutating action logged with user + timestamp |
| Session | Stateless | Token with expiry |

### Roles

| Role | Ops Portal | Admin Portal |
|------|-----------|-------------|
| Viewer | Read dashboards, metrics | Read instrument/participant config |
| Operator | + Halt/resume, kill switch, EOD trigger | Read only |
| Admin | Full ops access | + Create/edit instruments, participants, risk limits |
| Super Admin | Full access | + User management, config approval |

### High-Risk Controls

These require explicit confirmation dialog + audit entry:
- Kill switch activation
- Instrument halt
- Instrument delisting
- Risk limit changes
- Participant deactivation

### What Must Not Be Exposed

- Raw journal data
- Internal OrderIds or memory addresses
- Server private keys or cert details
- Other participants' order-level data
- Daemon process controls (start/stop/restart)

---

## 7. Page / Navigation Map

### Ops Portal

| Page | Purpose | Primary User | Main Components | Backend | MVP |
|------|---------|:------------:|----------------|---------|:---:|
| Dashboard | Health, instrument count, session count, key metrics | Operator | Status cards, metric counters, instrument state table | health, instruments, metrics | Yes |
| Instruments | All instruments with state, bid/ask, controls | Operator | Data table, halt/resume buttons, state badges | instruments, halt/resume | Yes |
| Sessions | FIX sessions with CompID, seq numbers, status | Operator | Data table, disconnect button | **New API** | Yes |
| Kill Switch | Participant-level trading block controls | Operator | Participant list, activate/deactivate buttons | **New API** | Yes |
| Metrics | Full metrics view with auto-refresh | Operator | Metric cards, counter trends | metrics | Yes |
| EOD | Trigger EOD, view last result, download files | Operator | Trigger button, result display, file links | eod/trigger | Yes |
| Events | Recent FIX events timeline | Operator | Scrolling event list, severity filter | **New API** | Later |
| Alerts | Alert rules and active alerts | Operator | Alert table, config form | **Not built** | Later |

### Admin Portal

| Page | Purpose | Primary User | Main Components | Backend | MVP |
|------|---------|:------------:|----------------|---------|:---:|
| Instruments | CRUD for instruments | Admin | Data table, create/edit forms | **New write API** | Yes |
| Participants | CRUD for participants + CompID | Admin | Data table, create/edit forms | **New write API** | Yes |
| Risk Limits | View/edit per-participant risk config | Admin | Config form per participant | **New API** | Yes |
| Sessions | View/edit session schedule | Admin | Schedule editor | **New API** | Later |
| Audit Log | All config change history | Admin | Searchable log table | **Not built** | Later |
| Users | User/role management | Super Admin | User table, role assignment | **Not built** | Later |

---

## 8. MVP vs Later Scope

### A. Portal MVP

| Item | Portal | Depends On |
|------|--------|-----------|
| Dashboard (health, instruments, metrics) | Ops | Existing APIs only |
| Instrument halt/resume UI | Ops | Existing APIs |
| EOD trigger UI | Ops | Existing API |
| FIX session view | Ops | New session state API |
| Kill switch UI | Ops | New HTTP wrapper |
| Instrument read/create/edit | Admin | New ref-data write API |
| Participant read/create/edit | Admin | New ref-data write API |
| Risk limits view | Admin | New risk read API |
| Auth shell (user login) | Shared | New auth service |

### B. Strong Next Additions

- Metrics trend charts (not just current values)
- Risk limit editing
- Session schedule editing
- Confirmation dialogs on destructive actions
- Role-based navigation filtering

### C. Later Enhancements

- Event timeline with streaming
- Alert configuration and notification
- Config change audit trail
- Config approval workflows
- User/role management UI

### D. Explicitly Out of Scope

- Order-level book visualization
- Trade blotter / participant-facing portal
- Journal/snapshot management UI
- Daemon process management
- Mobile app

---

## 9. Recommended Build Sequencing

| Step | What | Why This Order | Effort |
|:----:|------|---------------|:------:|
| 1 | **Backend API wrappers** — expose kill switch, FIX session state, ref-data write, risk config read via HTTP | Portal is useless without backend contracts | M |
| 2 | **Auth service** — user login, JWT tokens, role model | Portal is insecure without auth | M |
| 3 | **Ops Dashboard (read-only)** — health, instruments, metrics, sessions | Lowest risk; immediate value; proves UI stack | S |
| 4 | **Ops Controls** — halt/resume, kill switch, EOD trigger buttons | High-value ops actions behind confirmation dialogs | S |
| 5 | **Admin Instruments** — CRUD for instruments | First admin write surface | M |
| 6 | **Admin Participants** — CRUD for participants + risk limits | Replaces manual JSON editing | M |
| 7 | **Audit logging** — track all mutating portal actions | Required before production use | M |
| 8 | **Advanced ops** — event timeline, alert config | Nice to have after core is stable | L |

**Total estimated MVP (steps 1-6)**: 6-10 weeks with 1-2 frontend engineers.

---

## 10. Risks / Anti-Patterns

| # | Anti-Pattern | Why It's Dangerous | Mitigation |
|:-:|-------------|-------------------|------------|
| 1 | **Building UI before backend APIs exist** | Creates throwaway frontend code when APIs change | Build backend wrappers first (Step 1) |
| 2 | **Mixing ops and admin in one interface** | Operators accidentally change config; admins accidentally halt trading | Separate navigation sections with role gates |
| 3 | **Exposing dangerous controls without confirmation** | Accidental halt or kill switch activation | Confirmation dialogs + audit log on all mutating actions |
| 4 | **Making the portal the ONLY way to operate** | Portal down = platform unmanageable | CLI/curl must always work; portal is a convenience layer |
| 5 | **Embedding business logic in the frontend** | Validation drift between UI and backend; security bypass via API | All validation and authorization in backend; UI is presentation only |
| 6 | **Showing raw internal fields** | Composite OrderIds, memory addresses, internal sequence numbers confuse operators | Map internal IDs to human-readable labels |
| 7 | **Building a monitoring stack in the portal** | Grafana/Prometheus already do this better | Portal shows summary metrics; link to Grafana for deep dive |
| 8 | **Over-designing the auth system early** | Full RBAC is complex; API key + 2 roles is sufficient for MVP | Start with Operator + Admin roles; extend later |

---

## 11. Final Recommendation

### When to Build

**Phase 2B** (after Phase 2A completes), with this gating:

| Prerequisite | Why |
|-------------|-----|
| Phase 2A complete | Auto EOD, TOB qty, FIX multi-message must be stable |
| Backend API wrappers built | 7 new APIs needed before any UI work |
| Auth service designed | Portal without auth is a security regression |
| At least one real external participant has piloted | Portal requirements should be informed by real participant feedback |

### What to Build First

1. Backend API wrappers (Step 1) — can start in late Phase 2A
2. Read-only Ops Dashboard — immediate value, lowest risk
3. Ops Controls (halt/resume/kill switch/EOD) — highest operator value

### What Should Wait

- Admin write surfaces — until ref-data write APIs are stable
- Audit logging — until auth service exists
- Event timeline — until structured log streaming is built
- Config approval workflows — until multi-user auth exists

### Technology Recommendation (Deferred)

Technology choice is not yet decided. Candidates:
- React + shadcn/ui (modern, widely available talent)
- Simple server-rendered HTML (lowest complexity, no build toolchain)
- Retool / admin panel tool (fastest for internal-only use)

Decision should be made when portal build is approved, not now.
