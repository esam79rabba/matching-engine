# Three-Target Outreach Pack

**Date**: 2026-03-27
**Purpose**: Tailored outreach for first real participant pilot
**Status**: Ready to send — fill [BRACKETS] before sending

---

## Target 1: VersiFi

### Outreach Message

Subject: **Mutual Connectivity Test — ADGM Digital Asset Matching Engine**

Dear Alexander / Daniel,

We are building an electronic matching engine targeting ADGM-regulated digital asset markets. We noticed VersiFi's FSRA In-Principle Approval for DMA and OTC trading, and believe there is a clear mutual benefit in an early connectivity test.

**What we're proposing:**
A controlled 2-hour FIX 4.4 integration test between your DMA client and our matching engine. Two spot instruments (BTC-USD, ETH-USD), standard order lifecycle (submit, fill, cancel), and real-time market data.

**What you get:**
- Early validation of your DMA connectivity path against an ADGM-native venue engine
- Direct feedback loop with our engineering team
- No production commitment, no fees, no contractual obligation

**What we get:**
- Real institutional participant feedback on our FIX integration
- Evidence for our own FSRA pre-application preparation

**Our current state:**
- FIX 4.4 over TCP with session persistence
- Price-time FIFO matching, pre-trade risk, market data feed
- 442 automated tests, Docker-containerized deployment
- Ops portal with RBAC, metrics, and audit trail
- This is a prototype pilot, not a production launch

**Logistics:**
- We provide: onboarding guide, connection details, dedicated CompID
- You provide: a FIX client connection for ~2 hours
- Timeline: at your convenience within the next 2-3 weeks

Happy to schedule a 30-minute call to walk through the details.

Best regards,
[YOUR NAME]
[YOUR TITLE]

### Rationale
VersiFi is explicitly building DMA to digital asset venues from ADGM. They need venues to test against. This is peer-to-peer infrastructure validation, not a vendor pitch.

### Objection Handling

| Objection | Response |
|-----------|----------|
| "We're still in IPA stage ourselves" | "That's exactly why this is mutual — we're both validating. No production dependency on either side." |
| "Is this production-ready?" | "No — it's a validated prototype with 442 tests. We're transparent about that. The pilot tests connectivity, not production readiness." |
| "What's your FSRA status?" | "Pre-application. The pilot evidence supports our regulatory preparation, same as yours." |

### Limitations to Disclose
- Prototype on cloud infrastructure, not production hardware
- FIX over plain TCP (TLS available locally, Docker TLS issue deferred)
- Continuous trading only (no auctions)
- Manual participant reconnect after any restart
- Not FSRA-licensed — pre-application stage

### The Ask
2-hour controlled connectivity pilot. FIX 4.4. No production commitment. No fees. Mutual benefit.

---

## Target 2: QCP Group

### Outreach Message

Subject: **FIX Integration Pilot — ADGM Digital Asset Matching Engine**

Dear QCP Connectivity Team,

We are developing an electronic matching engine for ADGM-regulated digital asset markets. Given QCP's full FSRA license and active market-making operations from Abu Dhabi, we would value your feedback on our FIX integration.

**The proposal:**
A controlled 2-hour connectivity test — standard FIX 4.4 order lifecycle against our matching engine. Two spot instruments, real fills, real market data.

**Why QCP:**
As a licensed ADGM market maker, your connectivity standards define what venue infrastructure must support. Early feedback from your team helps us build to institutional expectations rather than assumptions.

**Our current state:**
- FIX 4.4 with session persistence and gap recovery
- Price-time FIFO matching with pre-trade risk controls
- Per-endpoint RBAC, audit trail, Prometheus metrics
- Journal replication with automated failover detection
- 442 automated tests, containerized deployment
- Prototype — not production-certified, not FSRA-licensed

**What we provide:**
- Full FIX message specification and onboarding guide
- Dedicated CompID and session configuration
- Engineering support during the test

**What we ask:**
- One FIX session from your connectivity/test environment for ~2 hours
- Honest feedback on integration experience

No production commitment. No commercial terms. We're building and want informed feedback.

Best regards,
[YOUR NAME]
[YOUR TITLE]

### Rationale
QCP is the strongest institutional profile in ADGM digital assets. They have the infrastructure to connect quickly and the expertise to provide meaningful feedback. Their participation would be strong validation evidence.

### Objection Handling

| Objection | Response |
|-----------|----------|
| "Are you FSRA-licensed?" | "Not yet — we're in pre-application. This pilot generates connectivity evidence for our regulatory preparation." |
| "What's your production timeline?" | "We're focused on getting the technology right first. This pilot validates whether our FIX integration meets institutional standards." |
| "Where's the volume?" | "This is a connectivity test, not a liquidity event. We're validating order lifecycle correctness, not market depth." |
| "We only connect to production venues" | "We understand. If a pre-production test doesn't fit your current priorities, we'd welcome the conversation when we're closer to go-live." |

### Limitations to Disclose
- Prototype — not production-certified
- Not FSRA-licensed (pre-application)
- FIX over plain TCP on public host
- No clearing integration
- Two instruments only (BTC-USD, ETH-USD)
- Cloud infrastructure, not colocated

### The Ask
2-hour controlled connectivity test. FIX 4.4. No production commitment. Feedback-driven.

---

## Target 3: Global Crypto Market Maker

### Outreach Message

Subject: **Early Connectivity Test — New ADGM Digital Asset Venue**

Dear Connectivity / Integration Team,

We are building an electronic matching engine for institutional digital asset trading, targeting ADGM/FSRA regulation. We are looking for experienced market makers willing to do a short controlled FIX integration test before we go to production.

**What we're offering:**
- 2-hour controlled pilot: FIX 4.4, BTC-USD + ETH-USD spot
- Full order lifecycle: submit, fill, cancel, halt/resume
- Real-time market data feed (TCP/JSON)
- Dedicated CompID and engineering support

**Why this is worth 2 hours:**
- Early look at a new regulated venue before it goes live
- Direct feedback channel to the engineering team
- No integration fees, no commitment, no NDA required
- If you like what you see, you're first in line when we launch

**Our technical stack:**
- Price-time FIFO matching engine (C++, 442 automated tests)
- FIX 4.4 with session persistence and bounded gap recovery
- Pre-trade risk controls (fat-finger, rate limit, kill switch)
- Journal replication with automated failover
- Docker-containerized, publicly accessible test environment

**What we need from you:**
- A FIX 4.4 client pointed at our test host for ~2 hours
- Feedback on connectivity experience

This is a prototype pilot — we're transparent about that. We're building seriously and want institutional validation early.

Best regards,
[YOUR NAME]
[YOUR TITLE]

### Rationale
Global market makers (Wintermute, GSR, Amber, DWF, Keyrock) connect to dozens of venues. They have dedicated connectivity teams and are accustomed to sandbox/pilot testing. Low friction to say yes.

### Objection Handling

| Objection | Response |
|-----------|----------|
| "We prioritize venues with volume" | "Understood. This is a connectivity test, not a liquidity commitment. 2 hours, no ongoing obligation." |
| "When do you go live?" | "We're targeting ADGM FSRA licensing. The pilot validates our technology readiness for that process." |
| "We need SBE/binary, not just FIX" | "FIX 4.4 is our primary gateway. Binary SBE codec exists but isn't on the pilot path yet. FIX covers the connectivity validation." |
| "What's different about your venue?" | "ADGM-native architecture, built for regulated institutional trading. Multi-asset core designed for compliance-by-configuration." |

### Limitations to Disclose
- Prototype, not production
- Not yet licensed in any jurisdiction
- FIX over plain TCP (TLS available but Docker deployment issue)
- Two instruments only
- No clearing integration
- Cloud infrastructure

### The Ask
2-hour controlled connectivity test. FIX 4.4. No commitment. Early access to a new regulated venue.

---

## Outreach Sequence (Same for All Targets)

| Day | Action |
|:---:|--------|
| 0 | Send outreach email / LinkedIn message |
| 1-5 | Wait for response |
| 5 | Follow up once if no response |
| 7 | If confirmed: provision CompID, send onboarding pack |
| 8-9 | 30-min onboarding call |
| 10 | Participant dry-run (15 min connectivity test) |
| **11** | **Pilot day (2 hours)** |

## Priority Order

Send all three in parallel. First to confirm gets the pilot slot.

| Priority | Target | Contact Method |
|:--------:|--------|---------------|
| 1 | VersiFi | LinkedIn (Alexander Kouperman, CTO) or via versifi.io |
| 2 | QCP Group | LinkedIn or via qcpgroup.com contact |
| 3 | Global MM | LinkedIn to head of connectivity / partnerships |
