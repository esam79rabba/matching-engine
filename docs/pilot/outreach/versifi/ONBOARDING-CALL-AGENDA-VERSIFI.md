# Onboarding Call Agenda — VersiFi

**Duration**: 30 minutes
**Attendees**: [Your name] + VersiFi integration contact (likely CTO or CISO)

---

## 1. Introductions and Context (3 min)

- Our platform: ADGM-targeted digital-asset matching engine
- Current status: FR-1 prototype, controlled pilot phase
- Why VersiFi: aligned jurisdiction, institutional focus, DMA model

## 2. VersiFi's Integration Context (5 min)

Questions for VersiFi:
- What FIX client are you using? (QuickFIX, vendor OMS, custom)
- Do you have a FIX connectivity test environment available?
- Any specific FIX features or message types you need beyond standard NewOrderSingle/Cancel?
- Any timeline constraints from your FSRA IPA process?

## 3. Connection Setup (7 min)

- Walk through connection details sheet
- Confirm: SenderCompID=VERSIFI, TargetCompID=VENUE
- Confirm: TLS certificate received and installable
- Confirm: host IP and ports accessible from their network
- Confirm: heartbeat interval (30 sec)

## 4. Message Format (5 min)

- NewOrderSingle (35=D) — tags 11, 55, 54, 40, 44, 38, 59
- ExecutionReport (35=8) — tags 150, 37, 31, 32
- Cancel (35=F) — must use tag 37 value in tag 41
- String ClOrdIDs fully supported
- Two instruments: BTC-USD (100), ETH-USD (101)

## 5. Known Limitations (3 min)

Brief walk-through of the 8 limitations. Key points:
- This is a prototype pilot, not production
- No auction, no DR, bounded gap recovery
- Market data is TCP/JSON
- EOD triggered manually

**Ask**: Does VersiFi acknowledge these for pilot purposes?

## 6. Pilot Day Plan (5 min)

- Proposed date: [TARGET DATE]
- Duration: ~2 hours
- Scenarios: logon → orders → trades → cancels → halt observation
- Communication channel during pilot: [Slack/Teams/phone]
- We handle all ops; VersiFi focuses on FIX integration testing

## 7. Next Steps (2 min)

- Schedule connectivity dry-run (15 min, 1-2 days before pilot)
- Confirm pilot date
- Exchange escalation contacts
