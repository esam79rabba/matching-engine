# Onboarding Email — Pilot Delta Trading

**Status**: DRAFT — fill in [BRACKETED] items before sending

---

Subject: **Matching Engine Pilot — Connection Package**

Dear [Contact Name],

Thank you for participating in our controlled pilot. Attached you will find everything needed to connect.

**Package contents:**
1. Participant Onboarding Guide (detailed FIX message reference)
2. Server TLS Certificate (pilot.yourdomain.com)
3. Connection Details (host, port, CompID, instruments)

**Your connection details:**
- SenderCompID: `PARTICIPANT_6`
- TargetCompID: `VENUE`
- Host: `pilot.yourdomain.com`
- FIX Port: `19876` (TLS required)
- Market Data: `19877` (TCP/JSON)

**Pilot date:** 2026-03-31

**Before the pilot:**
- Install the attached TLS certificate in your FIX client's trust store
- Configure your session with the CompID and host above
- We will schedule a 30-minute onboarding call to walk through the guide and test connectivity

**Known limitations (documented in the onboarding guide):**
- Prototype on development infrastructure, not production-certified
- Continuous trading only (no auctions)
- Market data is TCP/JSON
- No DR failover during pilot (warm restart available)

Please reply to confirm receipt and suggest a time for the onboarding call (30 min, before March 30).

Best regards,
[YOUR NAME]
[YOUR TITLE]
