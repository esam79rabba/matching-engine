# Onboarding Call Agenda

**Duration**: 30 minutes
**Attendees**: Venue operator + participant integration contact
**Prerequisites**: Participant has received onboarding package

---

## Agenda

### 1. Welcome and Context (3 min)

- Pilot purpose: validate connectivity and order lifecycle
- This is a controlled prototype pilot, not production
- Session will be [duration] on [date]

### 2. Connection Setup Review (7 min)

- Confirm CompID received and configured
- Confirm TLS certificate installed / trusted
- Confirm FIX client version and type (QuickFIX, vendor, custom)
- Confirm FIX host/port accessible from participant network
- Confirm heartbeat interval (30 sec)

### 3. Message Format Walkthrough (5 min)

- NewOrderSingle (35=D) — key tags: 11, 55, 54, 40, 44, 38, 59
- ExecutionReport (35=8) — key tags: 150, 37, 31, 32, 39
- OrderCancelRequest (35=F) — use tag 37 value from ack in tag 41
- String ClOrdIDs supported

### 4. Known Limitations (5 min)

Walk through Section 11 of onboarding guide:
- Prototype performance (not production-certified)
- No DR, no auction, no advanced order types
- Market data TCP/JSON (top-of-book qty may show 0)
- EOD manual trigger
- Cancel requires exact venue OrderID

**Ask**: Does the participant acknowledge these limitations for the pilot?

### 5. Pilot Day Plan (5 min)

- Target date/time
- Session schedule (PRE_OPEN → CONTINUOUS → CLOSED)
- Planned test scenarios: logon → order → trade → cancel → halt observation
- Communication channel during pilot (Slack, phone, email)
- Incident handling: operator stops trading if critical issue

### 6. Connectivity Test (3 min)

If possible during the call:
- Participant attempts TCP/TLS connection to FIX port
- Verify handshake success
- If not possible: schedule a 15-minute connectivity test before pilot day

### 7. Questions and Next Steps (2 min)

- Participant questions
- Confirm: pilot date, communication channel, escalation contact
- Next step: connectivity test (if not done), then pilot day

---

## Post-Call Actions

| Action | Owner | Due |
|--------|-------|-----|
| Record call notes in operator notes | Operator | Same day |
| Confirm connectivity test scheduled | Operator | Within 2 days |
| Participant confirms readiness | Participant | Before pilot day |
