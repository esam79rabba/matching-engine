# Gateway Session State Model — Draft

**Status**: Starter artifact — decision-independent
**Source**: §12.2 Gateways, §13 External Interfaces (frozen baseline)

---

## Session State Machine

All gateway types share a common session state model:

```
DISCONNECTED ──→ AUTHENTICATING ──→ ACTIVE ──→ DISCONNECTING ──→ DISCONNECTED
                      │                │              │
                      ▼                ▼              ▼
                   REJECTED       SUSPENDED     FORCE_DISCONNECTED
                                     │
                                     ▼
                                  ACTIVE (resumed)
```

| State | Description | Orders Accepted? |
|-------|-------------|:----------------:|
| DISCONNECTED | No TCP connection | No |
| AUTHENTICATING | Connection established; credentials being verified | No |
| ACTIVE | Authenticated; fully operational | Yes |
| SUSPENDED | Temporarily paused (admin action or rate limit) | No |
| DISCONNECTING | Graceful logout in progress | No |
| FORCE_DISCONNECTED | Abrupt disconnection detected | No (CoD triggers) |
| REJECTED | Authentication failed; connection closed | No |

## FIX Session Handling

### Logon Flow
```
Client                          FIX Gateway                      Risk/Auth Service
  │                                │                                   │
  │── TCP connect ────────────────→│                                   │
  │── Logon(CompID, pwd, HB=30s)─→│                                   │
  │                                │── Verify CompID + credentials ──→│
  │                                │←── Auth result (PASS/FAIL) ──────│
  │                                │                                   │
  │   [If PASS]:                   │                                   │
  │←── Logon(ack, server seq#) ───│                                   │
  │   State: ACTIVE                │                                   │
  │                                │                                   │
  │   [If FAIL]:                   │                                   │
  │←── Logout(reason) ────────────│                                   │
  │   TCP closed                   │                                   │
```

### Heartbeat / Liveness
- HeartBtInt negotiated at Logon (e.g., 30 seconds)
- If no message received within HeartBtInt → send TestRequest
- If TestRequest not answered within HeartBtInt → declare disconnect
- Missed heartbeat count tracked: configurable threshold (default: 2 misses)

### Sequence Number Management
- Each session maintains independent inbound and outbound sequence counters
- On Logon: sequence reset (configurable: reset-on-logon or continue)
- Gap detected by receiver: send ResendRequest(BeginSeqNo, EndSeqNo)
- Sender responds with SequenceReset-GapFill for admin messages, resends application messages
- All sequence gaps logged for audit

### Disconnect and Reconnect
- On disconnect: session enters FORCE_DISCONNECTED
- Cancel-on-disconnect evaluated (see below)
- Client reconnects: new TCP connection → Logon → sequence gap recovery → ACTIVE
- Session state (orders, positions) maintained across reconnect if within same trading day

## Binary Gateway Session Handling

### Logon Flow
- Simplified binary logon message: SessionToken + AccountId
- Session token obtained via out-of-band API (REST endpoint or certificate-based exchange)
- Binary logon does not use FIX CompID/password — uses mTLS client certificate + session token

### Heartbeat
- Binary heartbeat: 8-byte message (TemplateId=22, SequenceNumber, SendingTime)
- Interval: configurable (default: 5 seconds — tighter than FIX due to latency sensitivity)
- Miss threshold: 2 (configurable)

### Sequence Management
- Same partition-aware sequencing as FIX sessions
- Binary sessions support explicit sequence reset at session start
- No ResendRequest mechanism — binary clients use snapshot recovery for gaps

## Drop Copy Session

- Read-only FIX session (separate CompID from order-entry session)
- Receives: ExecutionReport(8), OrderCancelReject(9) for all orders belonging to the participant
- No inbound application messages accepted (only session-level: Logon, Logout, Heartbeat, ResendRequest)
- Sequence numbers independent from order-entry session
- Gap recovery via ResendRequest

## Cancel-on-Disconnect (CoD)

### Trigger Conditions
```
Session enters FORCE_DISCONNECTED
    │
    ▼
Is CoD enabled for this session? ── No ──→ No action; orders remain
    │
    Yes
    │
    ▼
Start grace period timer (configurable: 0–5000 ms)
    │
    ├── Client reconnects during grace period ──→ Cancel CoD; session resumes
    │
    └── Grace period expires ──→ Execute CoD
                                    │
                                    ▼
                              For each resting order on this session:
                                - Generate synthetic CancelOrder message
                                - Route through sequencer (deterministic)
                                - Engine cancels order; publishes cancel event
                                - Audit: reason = CANCEL_ON_DISCONNECT
```

### CoD Configuration (per session)
| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| cod_enabled | boolean | false | Enable CoD for this session |
| cod_grace_period_ms | uint32 | 0 | Grace period before cancellation |
| cod_scope | enum | ALL_SESSION_ORDERS | What to cancel: all orders, or only DAY orders (retain GTC) |

### Important: CoD cancellations are deterministic
- Synthetic cancel messages are sequenced through the normal sequencer
- They appear in the journal as regular cancel events with reason=COD
- Replay will produce the same cancellations

## Throttling

### Per-Session Rate Limiting
| Parameter | Default | Description |
|-----------|---------|-------------|
| max_messages_per_second | 1000 | Total inbound messages per second |
| max_new_orders_per_second | 500 | New order submissions per second |
| max_cancels_per_second | 2000 | Cancel requests per second (higher to allow rapid exit) |
| burst_window_ms | 100 | Burst measurement window |
| burst_multiplier | 3 | Allowed burst = rate × multiplier for burst_window |

### Throttle Enforcement
- Measured at gateway before forwarding to risk service
- Exceeded → Reject with reason=THROTTLED (rejection code 8)
- Throttle events logged and published to surveillance
- Persistent violation → session may be suspended (operator action)

## Session-to-Engine Mapping

```
Session (FIX or Binary)
    │
    ├── ParticipantId (from auth)
    ├── AccountId (from order or session config)
    ├── SessionId (venue-assigned, unique per connection)
    │
    ▼
Orders carry: ParticipantId + AccountId + SessionId
    │
    ▼
CoD scope: by SessionId (only orders from this specific session)
Kill switch scope: by ParticipantId (all sessions for that participant)
```

## Certification Implications

Participants must demonstrate in certification (per §23):
- Clean logon/logout (CERT-001, CERT-019)
- Invalid logon handling (CERT-002)
- Heartbeat exchange (CERT-003)
- Sequence gap recovery (CERT-004)
- Disconnect and CoD behavior (CERT-020)
- Rate limit handling (CERT-018)
- Drop copy completeness (CERT-021)
