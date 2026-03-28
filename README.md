# Matching Engine

A production-grade multi-asset electronic matching engine platform built from the ground up in C++20. Designed for regulated digital asset venues with institutional-grade connectivity, deterministic matching, surveillance, and compliance-by-configuration.

## At a Glance

| Metric | Value |
|--------|:-----:|
| Language | C++20 |
| Tests | 643 (100% passing) |
| Source | ~40,000 lines |
| Modules | 17 |
| Build | CMake + Google Test |
| Deployment | Docker (multi-stage) |
| FIX Protocol | 4.4 (custom + QuickFIX C++) |
| Portal | React + Vite + TypeScript |
| Target Jurisdiction | ADGM / FSRA |

## Architecture

```
                    ┌─────────────────────────────────────────────────────────┐
                    │                    Operations Portal                     │
                    │          React + Vite + TypeScript (9 pages)            │
                    │     Dashboard, Instruments, Sessions, Kill Switch,      │
                    │     Metrics, EOD, Audit, Admin CRUD                    │
                    └────────────────────────┬────────────────────────────────┘
                                             │ HTTP + WebSocket
                                             ▼
Participant ─→ FIX Gateway (TLS/mTLS) ─→ Pre-Trade Risk ─→ Matching Engine ─→ Output Bus
            ─→ REST API (HTTP/JSON)   ─→  (Position +     │  (FIFO or       │
            ─→ SBE Binary Gateway          Credit +       │   Pro-Rata)     │
                                           Rate Limiter)  │                 │
                    ┌─────────────────────────────────────┘                 │
                    ├─→ Market Data (TCP/JSON + UDP Multicast + L2 Depth)   │
                    ├─→ Drop Copy Service                                   │
                    ├─→ Trade Store + Fee Calculator                        │
                    ├─→ Journal (Direct I/O, CRC32, Replication)           │
                    ├─→ Surveillance (4 detection rules, alert pipeline)   │
                    ├─→ Audit Trail (SHA-256 hash chain)                   │
                    └─→ Ops Console (RBAC, Metrics, WebSocket push)        │
```

**Single-threaded per partition.** No locks, no shared mutable state, deterministic by construction.

## Core Capabilities

### Matching Engine
- **Price-time FIFO** priority (default)
- **Pro-rata allocation** — proportional fill within price level (configurable per instrument)
- **5 order types**: Market, Limit, Stop, Stop-Limit, Trailing Stop
- **8 time-in-force**: Day, GTC, GTD, IOC, FOK, AuctionOnly, Market-on-Close, Market-on-Open
- **Iceberg/Reserve orders** — hidden quantity with display replenishment, time priority loss on refresh
- **Pegged orders** — Primary, Midpoint, Market peg with offset and limit cap, auto-reprice on BBO change
- **Post-only enforcement** — reject if would immediately cross
- **Reduce-only orders** — reject if would increase position (integrated with position tracker)
- **Mass quote** — two-sided atomic insert/replace for market makers
- **4 STP modes**: CancelNewest, CancelOldest, CancelBoth, Decrement
- **4 auction types**: Opening, Closing, Volatility (auto-trigger on dynamic band breach), Scheduled
- **Auction imbalance publication** — indicative price, side, quantity during collection
- Integer-encoded prices and quantities (no floating point on hot path)
- Pre-allocated order pool (zero allocation on hot path)

### Connectivity
- **FIX 4.4** custom parser + **QuickFIX C++** session layer (conditional, `-DENABLE_QUICKFIX=ON`)
- **REST API** — full order management (submit, cancel, query) + L2 book depth
- **TLS v1.3** with mTLS support via transport abstraction
- **SBE** binary codec (encode/decode)
- **WebSocket** server for real-time push (trades, depth, status, alerts, metrics)
- FIX session persistence, bounded gap recovery (ResendRequest / SequenceReset)
- Persistent FIX message store (file-backed, survives restart)

### Risk Controls
- **Position limits** — per-participant per-instrument long/short caps, real-time tracking
- **Credit/exposure limits** — net debit tracking, configurable per participant
- **Dynamic price bands** — auto-recalculate from last traded price
- **Static price bands** — deviation from reference price
- Fat-finger price/quantity/notional checks (market orders use reference price)
- **Sliding-window rate limiter** — per-session message throttling
- Participant-level kill switch
- Cancel-on-disconnect (wired into FIX gateway disconnect handler)
- Volatility auction auto-trigger on dynamic band breach

### Market Surveillance
- **Surveillance framework** — pluggable rule engine with event bus and alert pipeline
- **Wash trade detection** — Critical alert when same participant is buyer and seller
- **Layering/spoofing detection** — High alert on excessive cancel ratio
- **Order-to-trade ratio** — Medium alert when ratio exceeds threshold
- **Quote stuffing detection** — High alert on abnormal message rate
- Alert severity classification (Low/Medium/High/Critical)

### Post-Trade
- **Trade store** — record and query trades by participant, instrument, time range
- **Fee calculator** — real-time maker/taker fee calculation per instrument
- **Drop copy service** — per-participant FIX execution reports
- **EOD processor** — trade summaries, VWAP, fees, instrument statistics

### Market Data
- **TCP/JSON** publisher (top-of-book + trades)
- **UDP multicast** publisher (configurable group/port)
- **L2 depth publisher** — full order book snapshots with sequence numbers
- **OHLC/VWAP statistics** — real-time per-instrument tracking
- **Snapshot recovery** — full book state for new subscribers
- Reference/opening/closing price publication

### Operations
- **React Portal** — 9-page TypeScript dashboard (Vite build)
- **RBAC** — viewer / operator / admin roles, per-endpoint enforcement
- **Prometheus metrics** — engine matches, orders resting, MD published, FIX sessions, journal writes
- **Audit trail** — SHA-256 hash chain, every order/cancel/fill/reject/modify recorded
- **Audit log** — operational actions (halt, kill switch, EOD)
- **WebSocket push** — real-time event streaming to portal
- Instrument halt / resume / close via authenticated API
- Session scheduler with automatic state transitions

### Durability & Recovery
- **DirectIOJournal** — O_DIRECT (Linux) / F_NOCACHE (macOS)
- Segmented journal with rotation and CRC32 checksums
- Snapshot capture and restore
- Warm restart (snapshot + journal replay) and cold start (full replay)
- Startup controller with post-replay state validation

### DR / Failover
- **Journal replication** — synchronous mode, zero RPO
- **Automated failure detection** — health probe (HEALTHY -> DEGRADED -> FAILED)
- **Automated promotion** — safety-validated with generation-counter fencing
- **Split-brain protection** — stale primary rejected by generation check

## Project Structure

```
MatchingEngine/
├── engine/core/          # Matching engine kernel (17 order types, pro-rata, auctions, stops)
├── gateway/fix/          # FIX 4.4 gateway + QuickFIX integration
├── gateway/sbe/          # SBE binary codec
├── risk/                 # Pre-trade risk: position, credit, rate limiter
├── surveillance/         # Market surveillance: 4 detection rules + alert engine
├── post-trade/           # Trade store + fee calculator
├── market-data/          # Publisher + TCP/UDP transport + depth + statistics
├── dropcopy/             # Drop copy service
├── refdata/              # Reference data service + HTTP handler
├── ops/                  # Ops controller, auth, RBAC, metrics, audit trail, WebSocket
├── eod/                  # End-of-day processor + file writer
├── common/               # Transport abstraction (TCP/TLS), JSON config loader
├── replication/          # Journal replicator, receiver, health probe, failover
├── certification/        # Client certification harness + report writer
├── daemon/               # Standalone daemon entry point + integration tests
├── simulator/            # Participant simulator (market maker behavior)
├── portal/               # React + Vite + TypeScript ops dashboard
├── deploy/               # Docker config, pilot-env, systemd, scripts
├── docs/                 # Specifications (33), pilot (23), runbooks (7), technical (25+)
├── Dockerfile            # Multi-stage build
├── docker-compose.yml    # Full stack (engine + portal)
└── benchmark/            # Matching benchmark
```

## Quick Start

### Build from Source

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
ctest --timeout 30    # 643 tests
```

**Requirements:** C++20 compiler (GCC 12+ / Clang 15+), CMake 3.20+, OpenSSL (for TLS/auth)

**Optional:** `-DENABLE_QUICKFIX=ON` for QuickFIX C++ session layer

### Docker

```bash
docker build -t matching-engine .
docker compose up -d

# Health check
curl http://localhost:18080/api/v1/health

# Portal
open http://localhost:80
```

### Run Daemon Directly

```bash
cd build
./daemon/matching-engine-daemon ../deploy/pilot-env/platform-localhost.conf
```

### Build Portal

```bash
cd portal
npm install
npm run build    # Output in dist/
npm run dev      # Dev server on http://localhost:5173
```

## API Endpoints

### Public (no auth)

| Method | Path | Description |
|:------:|------|-------------|
| GET | `/api/v1/health` | Platform health |
| GET | `/api/v1/instruments` | List instruments + TOB |
| GET | `/api/v1/instruments/{id}` | Instrument detail |
| GET | `/api/v1/instruments/{id}/book` | L2 order book depth |
| GET | `/api/v1/instruments/{id}/trades` | Recent trades |
| GET | `/api/v1/metrics` | Prometheus metrics |
| GET | `/api/v1/metrics/json` | JSON metrics |
| POST | `/api/v1/auth/login` | JWT login |
| GET | `/api/v1/fix/sessions` | Active FIX sessions |
| GET | `/api/v1/eod/status` | Last EOD result |
| GET | `/api/v1/audit` | Audit log entries |

### Authenticated

| Method | Path | Auth | Description |
|:------:|------|:----:|-------------|
| POST | `/api/v1/orders` | Bearer | Submit new order |
| POST | `/api/v1/orders/{id}/cancel` | Bearer | Cancel order |
| GET | `/api/v1/orders` | Bearer | Order history (by participant) |
| GET | `/api/v1/orders/{id}` | Bearer | Order detail |
| GET | `/api/v1/trades` | Bearer | Trade history |
| POST | `/api/v1/instruments/{id}/halt` | Operator | Halt instrument |
| POST | `/api/v1/instruments/{id}/resume` | Operator | Resume instrument |
| POST | `/api/v1/eod/trigger` | Operator | Trigger EOD |
| POST | `/api/v1/kill-switch/{pid}/activate` | Operator | Activate kill switch |
| POST | `/api/v1/instruments` | Admin | Create instrument |
| POST | `/api/v1/participants` | Admin | Create participant |

## Ports

| Port | Service | Protocol |
|:----:|---------|----------|
| 9876 | FIX Gateway | TCP/TLS (FIX 4.4) |
| 9877 | Market Data (TCP) | TCP (JSON) |
| 9878 | Market Data (Multicast) | UDP |
| 8080 | Ops Console + REST API | HTTP |
| 8081 | WebSocket | WS (RFC 6455) |
| 80 | Portal | HTTP (Nginx) |

## Testing

```bash
cd build
ctest --timeout 30                           # All 643 tests
ctest -R MatchingEngine                      # Core matching (31 tests)
ctest -R StopOrder                           # Stop orders (7 tests)
ctest -R TrailingStop                        # Trailing stops (5 tests)
ctest -R Iceberg                             # Iceberg orders (8 tests)
ctest -R PeggedOrder                         # Pegged orders (8 tests)
ctest -R ProRata                             # Pro-rata matching (7 tests)
ctest -R ClosingAuction                      # Closing auction (6 tests)
ctest -R VolatilityAuction                   # Volatility auction (11 tests)
ctest -R MassQuote                           # Mass quote (4 tests)
ctest -R Surveillance                        # Surveillance rules (16 tests)
ctest -R TradeAuditTrail                     # Audit trail (8 tests)
ctest -R PositionTracker                     # Position limits (6 tests)
ctest -R CreditTracker                       # Credit limits (5 tests)
ctest -R OrderApi                            # REST API (17 tests)
ctest -R WebSocket                           # WebSocket server (4 tests)
ctest -R DaemonFix                           # Daemon integration (19 tests)
ctest -R Replication                         # DR replication (8 tests)
ctest -R AutoPromote                         # Failover automation (11 tests)
```

## Architecture Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Hot-path language | C++20 | Latency + determinism |
| Partition model | Single-threaded per partition | No locks, provable determinism |
| Sequencing | Partition-local monotonic | No global sequencer bottleneck |
| Journal | Custom append-only with Direct I/O | Best alignment with C++ hot path |
| FIX session | Custom parser + QuickFIX (conditional) | Custom for speed, QuickFIX for certification |
| Price encoding | Integer (no floating point) | Deterministic matching |
| Matching | FIFO + Pro-rata | Configurable per instrument |
| Replication | Synchronous journal replication | Zero RPO for Regulatory-Grade |
| Failover | Automated detection + promotion | Generation-counter fencing for split-brain |
| Portal | React + Vite + TypeScript | Modern frontend, component architecture |
| Surveillance | Pluggable rule engine | Extensible detection rules |
| Audit trail | SHA-256 hash chain | Tamper-evident regulatory compliance |

## License

Proprietary. All rights reserved.
