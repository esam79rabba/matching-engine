# Reference Data API Specification — FR-1 Draft

**Status**: Starter API spec — decision-independent
**Source**: §12.4, §13.7, §17 (frozen baseline); reference data model draft
**Protocol**: REST over HTTPS (JSON)
**Auth**: OAuth 2.0 + RBAC (see §18 security architecture)
**Base path**: `/api/v1`

---

## Common Conventions

- **Versioning**: URL path (`/api/v1/`)
- **Content-Type**: `application/json`
- **Timestamps**: ISO 8601 with nanosecond precision where applicable
- **IDs**: Integer (uint32) venue-assigned; client may provide external references as strings
- **Pagination**: `?page=1&limit=50` (default limit=50, max=500)
- **Filtering**: Query parameters per endpoint
- **Audit**: All mutating operations logged with operator identity, timestamp, before/after values
- **Four-eyes**: Sensitive changes (parameter updates, instrument state) require `X-Approver-Id` header in Phase 2+. FR-1: single-operator.
- **Error format**: `{ "error": { "code": "INVALID_TICK", "message": "..." } }`

---

## 1. Instruments

### GET /instruments
List instruments with optional filters.
- **Query params**: `?status=LISTED&segment_id=1&type=EQUITY&page=1&limit=50`
- **Response**: `{ "data": [Instrument], "pagination": { "page": 1, "limit": 50, "total": 1234 } }`
- **Auth**: Read access (any authenticated role)

### GET /instruments/{id}
Get single instrument by venue ID.
- **Response**: `Instrument` object
- **Auth**: Read access

### POST /instruments
Create new instrument.
- **Body**: `{ "symbol": "ACME", "isin": "US012345...", "type": "EQUITY", "currency": "USD", "tick_size_table_id": 1, "lot_size": 1, "price_precision": 4, "price_multiplier": 10000, "partition_id": 1, "market_segment_id": 1, "session_profile_id": 1, "static_price_band_pct": 10.0, "dynamic_price_band_pct": 5.0, "priority_model": "PRICE_TIME", "status": "PRE_LISTED" }`
- **Validation**: symbol unique; tick_size_table_id exists; partition_id valid; price_precision ≤ 8
- **Response**: `201 Created` with created Instrument
- **Audit**: Logged with operator, timestamp
- **Auth**: Market Operator or above

### PUT /instruments/{id}
Update instrument fields (non-state fields only).
- **Body**: Partial update — only changed fields
- **Validation**: Cannot change instrument_id, symbol (immutable); tick changes effective per configuration
- **Response**: `200 OK` with updated Instrument
- **Audit**: Before/after values logged
- **Auth**: Senior Market Operator

### POST /instruments/{id}/state
Change instrument trading state.
- **Body**: `{ "new_state": "LISTED", "reason": "Approved for trading" }`
- **Valid transitions**: PRE_LISTED→LISTED, LISTED→SUSPENDED, SUSPENDED→LISTED, LISTED→DELISTED
- **Response**: `200 OK` with state change confirmation
- **Audit**: State transition logged
- **Auth**: Market Operator (halt/suspend); Senior Operator (delist)

---

## 2. Participants

### GET /participants
List with filters: `?status=ACTIVE&type=MARKET_MAKER`

### GET /participants/{id}
Single participant.

### POST /participants
Create participant.
- **Body**: `{ "legal_name": "...", "lei": "...", "membership_type": "TRADING_MEMBER", "jurisdiction_code": "US", "risk_tier": "STANDARD", "fee_tier_id": 1 }`
- **Validation**: LEI format (20 chars); unique legal_name + LEI combination
- **Auth**: Platform Admin

### PUT /participants/{id}
Update participant (non-ID fields).

### POST /participants/{id}/state
Change status: PENDING→ACTIVE, ACTIVE→SUSPENDED, SUSPENDED→ACTIVE, ACTIVE→TERMINATED

---

## 3. Accounts

### GET /participants/{participant_id}/accounts
List accounts for a participant.

### POST /participants/{participant_id}/accounts
Create account.
- **Body**: `{ "account_type": "HOUSE", "clearing_account_ref": "CLR-001", "currency": "USD" }`
- **Auth**: Participant Admin (own firm) or Platform Admin

### PUT /accounts/{id}
Update account.

---

## 4. Session Profiles

### GET /session-profiles
List all session profiles.

### GET /session-profiles/{id}
Single profile with phase definitions.

### POST /session-profiles
Create session profile.
- **Body**: `{ "name": "EQUITY_MAIN", "timezone": "America/New_York", "phases": [ { "name": "Pre-Open", "start_time": "07:00:00", "state": "PRE_OPEN" }, { "name": "Continuous", "start_time": "09:30:00", "state": "CONTINUOUS" }, { "name": "Close", "start_time": "16:00:00", "state": "CLOSED" } ] }`
- **Validation**: Phases must be chronologically ordered; states must form valid transitions
- **Auth**: Senior Market Operator

### PUT /session-profiles/{id}
Update profile. Changes effective at next session start (not mid-session).

---

## 5. Tick-Size Tables

### GET /tick-size-tables
List all tables.

### GET /tick-size-tables/{id}
Single table with bands.

### POST /tick-size-tables
Create table.
- **Body**: `{ "name": "LARGE_CAP", "bands": [ { "price_from": 0, "price_to": 0.4999, "tick_size": 0.0001 }, { "price_from": 0.50, "price_to": 0.9999, "tick_size": 0.0005 }, ... ] }`
- **Validation**: Bands must be contiguous and non-overlapping; tick_size > 0
- **Auth**: Senior Market Operator

---

## 6. Lot-Size Rules

### GET /lot-size-rules
### GET /lot-size-rules/{id}
### POST /lot-size-rules
- **Body**: `{ "instrument_group_id": 1, "round_lot": 100, "odd_lot_allowed": true, "min_quantity": 1, "max_quantity": 10000000, "quantity_precision": 0 }`

---

## 7. Fee Schedules

### GET /fee-schedules
### GET /fee-schedules/{id}
### POST /fee-schedules
- **Body**: `{ "name": "STANDARD_MAKER_TAKER", "maker_fee_rate": -0.0002, "taker_fee_rate": 0.0003, "volume_tiers": [ { "min_volume": 1000000, "maker_rate": -0.0003, "taker_rate": 0.0002 } ] }`
- **Note**: Fee rates are provisional — depend on fee model decision (not yet closed)
- **Auth**: Platform Admin

---

## 8. Holiday Calendars

### GET /calendars
### GET /calendars/{id}
### POST /calendars
- **Body**: `{ "name": "US_EQUITY", "holidays": ["2026-01-01", "2026-01-19", "2026-02-16", ...] }`
- **Note**: Specific dates depend on D-03 (jurisdiction) — use placeholder calendar until decided

### POST /calendars/{id}/holidays
Add individual holiday dates.

---

## 9. Market Segments

### GET /market-segments
### GET /market-segments/{id}
### POST /market-segments
- **Body**: `{ "name": "MAIN_BOARD", "description": "Primary trading board", "default_session_profile_id": 1 }`

---

## 10. Risk Limits

### GET /participants/{participant_id}/risk-limits
### PUT /participants/{participant_id}/risk-limits
Update risk limits (real-time effective — no restart needed).
- **Body**: `{ "instrument_group_id": 0, "max_order_price_pct": 10.0, "max_order_qty": 100000, "max_order_notional": 5000000, "max_message_rate": 1000, "max_open_orders": 10000 }`
- **Validation**: All values > 0; rate limits within platform capacity
- **Audit**: Before/after logged; alert if limits widened significantly
- **Auth**: Risk Manager

---

## 11. Bulk Operations

### POST /bulk/instruments
Bulk create instruments from CSV/JSON array.
- **Body**: Array of instrument definitions
- **Response**: `{ "created": 500, "errors": [ { "row": 42, "error": "duplicate symbol" } ] }`
- **Use case**: Initial instrument universe load

### POST /bulk/participants
Same pattern for participant bulk load.

---

## Error Codes

| Code | HTTP | Description |
|------|:----:|-------------|
| NOT_FOUND | 404 | Resource not found |
| DUPLICATE | 409 | Unique constraint violation |
| INVALID_FIELD | 400 | Field validation failure |
| INVALID_STATE_TRANSITION | 400 | State change not permitted |
| UNAUTHORIZED | 401 | Missing or invalid auth |
| FORBIDDEN | 403 | Insufficient role/permission |
| RATE_LIMITED | 429 | API rate limit exceeded |
| INTERNAL_ERROR | 500 | Unexpected server error |

---

## FR-1 vs Phase 2+ Scope

| Endpoint | FR-1 | Phase 2+ |
|----------|:----:|:--------:|
| Instruments CRUD | Yes | + corporate actions |
| Participants CRUD | Yes | + KYC integration |
| Accounts CRUD | Yes | + multi-currency |
| Session profiles | Yes | + half-day schedules |
| Tick-size tables | Yes | + regulatory auto-update |
| Fee schedules | Yes | + volume-tier automation |
| Holiday calendars | Yes | + multi-jurisdiction |
| Risk limits | Yes | + credit, position, exposure |
| Bulk operations | Yes | + incremental sync |
| Four-eyes approval | No | Yes |
| Change webhooks | No | Yes |
| Versioned history API | No | Yes |
