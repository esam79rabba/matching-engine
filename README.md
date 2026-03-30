# Matching Engine

A production-grade multi-asset electronic matching engine platform built from the ground up in C++20. Designed for regulated digital asset, equity, FX, commodity, and fixed-income venues with institutional-grade FIX connectivity, deterministic matching, real-time surveillance, and compliance-by-configuration.

## At a Glance

| Metric | Value |
|--------|:-----:|
| Language | C++20 (engine) + TypeScript (portals) |
| Tests | 729 |
| C++ Source | ~47,000 lines |
| TypeScript | ~6,000 lines |
| Instruments | 100 (6 asset classes, 6 partitions) |
| Modules | 17 C++ + 2 TypeScript apps |
| Build | CMake + Google Test (engine), Vite (portals) |
| Deployment | Docker multi-stage |
| FIX Protocol | 4.0 — 5.0 SP2 (QuickFIX C++ + custom parser) |
| Exchange Portal | React + Vite + TypeScript (20 pages) |
| Participant Terminal | React + Node.js + FIX client (8 pages) |
| Target Jurisdiction | ADGM / FSRA |

## Architecture

```
Participant Terminal (Node.js)          Exchange Portal (React)
  ┌──────────────────────┐              ┌──────────────────────┐
  │ 8 pages: Order Entry,│              │ 20 pages: Dashboard, │
  │ Order Book, Trades,  │              │ Instruments, Orders, │
  │ Positions, Market    │              │ Surveillance, Audit, │
  │ Data, Settings       │              │ Settlement, Admin... │
  └──────────┬───────────┘              └──────────┬───────────┘
             │ FIX 4.4 TCP                         │ HTTP + WebSocket
             ▼                                     ▼
  ┌─────────────────────────────────────────────────────────────┐
  │                    MATCHING ENGINE (C++20)                    │
  │                                                              │
  │  FIX Gateway (4.0-5.0 SP2) ─→ Pre-Trade Risk ─→ Matching   │
  │  REST API (HTTP/JSON)      ─→  (Position +     │  (FIFO or  │
  │  WebSocket (real-time)          Credit +       │   Pro-Rata) │
  │  SBE Binary Codec              Rate Limiter)  │             │
  │                                                │             │
  │  ├─→ Market Data (TCP + UDP Multicast + L2 Depth)           │
  │  ├─→ Surveillance (4 detection rules + alert pipeline)      │
  │  ├─→ Audit Trail (SHA-256 hash chain)                       │
  │  ├─→ Trade Event Bus → Clearing / Settlement (external)     │
  │  ├─→ Drop Copy Service                                      │
  │  ├─→ Journal (Direct I/O, CRC32, segmented)                │
  │  ├─→ Snapshot Persistence (binary, atomic write)            │
  │  └─→ DR Replication (sync, auto-failover, generation fence) │
  └─────────────────────────────────────────────────────────────┘
```

**Single-threaded per partition.** No locks, no shared mutable state, deterministic by construction. Optional LMAX Disruptor-style pipeline mode for pipelined multi-core processing.

## Instruments — 100 Across 6 Asset Classes

| Asset Class | Count | Partition | Matching | Examples |
|------------|:-----:|:---------:|:--------:|----------|
| Digital Assets | 20 | 1 | FIFO | BTC-USD, ETH-USD, SOL-USD, ETH-BTC |
| Equities | 24 | 2 | FIFO | AAPL, MSFT, NVDA, TSLA, ARAMCO, EMAAR, FAB |
| FX | 20 | 3 | Pro-Rata | EUR-USD, GBP-USD, USD-JPY, AED-USD |
| Commodities | 11 | 4 | Mixed | XAU-USD (Pro-Rata), WTI (FIFO), Coffee |
| Fixed Income | 12 | 5 | Pro-Rata | US10Y, German Bund, Sukuk 5Y, UAE Gov |
| Derivatives | 13 | 6 | FIFO | BTC-PERP, ETH futures, SPX futures, BTC options |

## Core Capabilities

### Matching Engine
- **Price-time FIFO** and **Pro-rata allocation** (configurable per instrument)
- **6 order types**: Market, Limit, Stop, Stop-Limit, Trailing Stop, Mass Quote
- **10 time-in-force**: Day, GTC, GTD (with expiry auto-cancel), IOC, FOK, AuctionOnly, MOC, MOO, IOC_BUDGET, FOK_BUDGET
- **Iceberg/Reserve orders** — hidden quantity with display replenishment
- **Pegged orders** — Primary, Midpoint, Market peg with offset and limit cap
- **Post-only**, **Reduce-only** enforcement
- **4 STP modes**: CancelNewest, CancelOldest, CancelBoth, Decrement
- **4 auction types**: Opening, Closing, Volatility (auto-trigger on band breach), Scheduled
- **Auction imbalance publication** with indicative price
- **Adaptive Radix Tree** order book (opt-in, `-DUSE_ART_ORDER_BOOK=ON`)
- **LMAX Disruptor pipeline** mode (opt-in, 3-stage: Risk → Match → Output)
- Pre-allocated order pool (zero allocation on hot path)
- Integer-encoded prices (no floating point on hot path)
- **GTD expiry checker** — automatic cancellation on expiry (1-second resolution)

### FIX Connectivity (4.0 — 5.0 SP2)
- **QuickFIX C++** session layer (conditional) + custom fallback parser
- **Multi-version**: FIX 4.0, 4.1, 4.2, 4.3, 4.4, FIXT.1.1 (5.0 SP2)
- **13 message types**: D (NewOrder), F (Cancel), G (Modify), H (OrderStatus), q (MassCancel), V (MarketDataRequest), 8 (ExecutionReport), r (MassCancelReport), AE (TradeCaptureReport), j (BusinessReject), W (MarketDataSnapshot), A (Logon), 5 (Logout)
- Session persistence, bounded gap recovery, heartbeat
- **Every order event notified** via FIX ExecutionReport (including GTD expiry, kill switch, STP cancels)
- **Passive fill notifications** — both sides of a trade receive ExecutionReports

### REST API
- Full order management: submit, cancel, query orders/trades
- L2 order book depth per instrument
- Account credit/position management
- Settlement callbacks and reconciliation
- 25+ endpoints with RBAC (viewer/operator/admin)

### WebSocket (Real-Time Push)
- RFC 6455 WebSocket server
- Channel-based pub/sub: trades, depth, status, alerts, metrics
- Every order event pushed to WebSocket subscribers

### Risk Controls
- **Position limits** — per-participant per-instrument long/short caps
- **Credit/exposure limits** — net debit tracking
- **Dynamic + static price bands** — auto-recalculate from last trade
- Fat-finger price/quantity/notional checks
- **Sliding-window rate limiter** per session
- Kill switch (participant-level)
- Cancel-on-disconnect
- Volatility auction auto-trigger

### Market Surveillance
- Pluggable rule engine with event bus and alert pipeline
- **Wash trade detection** (Critical)
- **Layering/spoofing detection** (High)
- **Order-to-trade ratio** monitoring (Medium)
- **Quote stuffing detection** (High)

### Post-Trade & Settlement Integration
- **Trade Event Bus** — pub/sub for external clearing, settlement, billing
- **Credit limit sync API** — external member accounting sets/queries limits
- **Position sync API** — real-time positions per participant
- **Settlement callbacks** — settlement system notifies after T+1/T+2
- **EOD reconciliation** — cross-system trade/position comparison
- Trade store + real-time fee calculator (maker/taker)
- Drop copy service per participant

### Market Data
- TCP/JSON publisher (top-of-book + trades)
- UDP multicast publisher (configurable)
- L2 depth publisher with sequence numbers
- OHLC/VWAP real-time statistics
- Snapshot recovery for new subscribers
- FIX Market Data (V/W) for book snapshots

### Persistence & Recovery
- **Snapshot file persistence** — binary format, field-by-field, atomic write
- Periodic snapshots (every 60 seconds) + on shutdown
- Orders survive daemon restarts
- DirectIO journal (O_DIRECT / F_NOCACHE) with CRC32
- Segmented journal with rotation
- **SHA-256 audit trail** — tamper-evident hash chain

### DR / Failover
- Journal replication (synchronous, zero RPO)
- Automated failure detection → validation → fencing → promotion
- Generation-counter split-brain protection

## Exchange Portal — 20 Pages

| Section | Pages |
|---------|-------|
| **Operations** | Dashboard, Instruments, FIX Sessions, Kill Switch, Metrics, EOD, Audit Log |
| **Trading** | Order Entry (all order types + GTD expiry picker), Order Book (L2 depth) |
| **History** | Trade History (with CSV export), Order History |
| **Accounts** | Credit/Exposure management, Positions |
| **Compliance** | Surveillance Alerts, Trade Audit Trail (hash chain) |
| **Settlement** | Settlement Status, Reconciliation |
| **Admin** | Instrument CRUD, Participant CRUD, Session Config (FIX 4.0-5.0 SP2) |

## Participant Terminal — 8 Pages

Separate Node.js/React application that connects to the exchange as a trading participant.

| Page | Description |
|------|-------------|
| Dashboard | Connection status, exchange health, summary stats |
| Order Entry | Submit buy/sell orders via FIX |
| Order Book | Live L2 depth (REST or FIX data source toggle) |
| Orders | Active + historical orders with cancel |
| Trades | Fill blotter with P&L |
| Positions | Net position per instrument with unrealized P&L |
| Market Data | Streaming instrument ticker cards |
| Settings | FIX connection config + auto-trader controls |

**Auto-trader**: 3 strategies (market-maker, random, aggressive) for simulation.

## Quick Start

### Build Engine from Source

```bash
mkdir build && cd build
cmake .. -DENABLE_QUICKFIX=OFF
make -j$(nproc)
ctest --timeout 30    # 729 tests
```

### Docker Deployment

```bash
export OPS_API_KEY=your-secret-key
export JWT_SIGNING_KEY=your-jwt-secret

docker compose build
docker compose up -d

# Verify
curl http://localhost:18080/api/v1/health
# Portal: http://localhost:80
```

### Run Participant Terminal (Local)

```bash
cd participant-terminal

# Install dependencies
cd server && npm install && cd ../ui && npm install && cd ..

# Start (pointing at exchange)
FIX_HOST=your-exchange-ip FIX_PORT=19876 \
EXCHANGE_URL=http://your-exchange-ip:18080 \
SENDER_COMP_ID=PARTICIPANT1 \
npm run dev

# Server: http://localhost:3001
# UI: http://localhost:5174
```

## Ports

| Port | Service | Protocol |
|:----:|---------|----------|
| 80 | Exchange Portal (Nginx) | HTTP |
| 9876 | FIX Gateway | TCP (FIX 4.0-5.0 SP2) |
| 9877 | Market Data | TCP (JSON) |
| 9878 | Market Data Multicast | UDP |
| 8080 | REST API | HTTP |
| 9880 | WebSocket | WS (RFC 6455) |

## Benchmark Results (Development Hardware)

| Operation | p50 | p99 | p99.9 | Throughput |
|-----------|:---:|:---:|:-----:|:----------:|
| Insert | 125 ns | 41 µs | 64 µs | 553K ops/s |
| Cancel | 208 ns | 3 µs | 5 µs | 509K ops/s |
| Modify | 125 ns | 3 µs | 5 µs | — |
| Match | 33 µs | 49 µs | 158 µs | 621K ops/s |

5 benchmark scenarios: single instrument, multi-instrument, sustained throughput, burst, with risk checks. HDR histogram percentile tracking.

## Project Structure

```
MatchingEngine/
├── engine/core/          # Matching engine kernel
├── gateway/fix/          # FIX gateway + QuickFIX integration
├── gateway/sbe/          # SBE binary codec
├── risk/                 # Pre-trade risk: position, credit, rate limiter
├── surveillance/         # Market surveillance: 4 rules + alert engine
├── post-trade/           # Trade store + fee calculator
├── market-data/          # Publisher + TCP/UDP + depth + statistics
├── dropcopy/             # Drop copy service
├── refdata/              # Reference data + HTTP handler
├── ops/                  # Auth, RBAC, metrics, audit trail, WebSocket
├── eod/                  # End-of-day processor
├── common/               # Transport abstraction (TCP/TLS), config loader
├── replication/          # DR: replicator, receiver, health, failover
├── certification/        # Client cert harness + report
├── daemon/               # Standalone daemon + integration tests
├── simulator/            # C++ participant simulator
├── portal/               # Exchange ops portal (React, 20 pages)
├── participant-terminal/ # Trading terminal (Node.js + React, 8 pages)
├── benchmark/            # Matching benchmark (HDR histogram)
├── deploy/               # Docker, systemd, configs, instruments
├── docs/                 # Specifications, pilot, runbooks, technical
├── Dockerfile            # Multi-stage build
└── docker-compose.yml    # Full stack deployment
```

## Architecture Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Hot-path language | C++20 | Latency + determinism |
| Partition model | Single-threaded per partition | No locks, provable determinism |
| Price encoding | Integer (no floating point) | Deterministic matching |
| Matching | FIFO + Pro-rata | Configurable per instrument |
| FIX session | QuickFIX C++ + custom fallback | Multi-version, certified + fast |
| Order book | std::map + opt-in ART | Simplicity + performance option |
| Pipeline | Optional LMAX Disruptor mode | Multi-core throughput |
| Persistence | Binary snapshot files | Immune to struct changes |
| Replication | Synchronous journal | Zero RPO |
| Failover | Auto-detect + promote | Generation-counter fencing |
| Portal | React + Vite + TypeScript | Modern, component architecture |
| Participant | Node.js + FIX TCP | Rapid development, real FIX |
| Surveillance | Pluggable rule engine | Extensible detection |
| Audit trail | SHA-256 hash chain | Tamper-evident compliance |

## License

Proprietary. All rights reserved.
