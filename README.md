# Matching Engine

A multi-asset electronic matching engine platform built from the ground up in C++20. Designed for regulated digital asset venues with institutional-grade connectivity, deterministic matching, and compliance-by-configuration.

## At a Glance

| Metric | Value |
|--------|:-----:|
| Language | C++20 |
| Tests | 444 (100% passing) |
| Source | ~27,000 lines |
| Modules | 14 |
| Build | CMake + Google Test |
| Deployment | Docker (multi-stage) |
| FIX Protocol | 4.4 with TLS v1.3 |
| Target Jurisdiction | ADGM / FSRA |

## Architecture

```
Participant ─→ FIX Gateway (TLS) ─→ Pre-Trade Risk ─→ Matching Engine ─→ Output Bus
                                                            │
                ┌───────────────────────────────────────────┘
                ├─→ Market Data Publisher (TCP/JSON)
                ├─→ Drop Copy Service
                ├─→ Journal (Direct I/O, CRC32)
                ├─→ Journal Replication (DR)
                └─→ Ops Console (HTTP, RBAC, Metrics)
```

**Single-threaded per partition.** No locks, no shared mutable state, deterministic by construction. Horizontal scaling via partitions.

## Core Capabilities

### Matching Engine
- Price-time FIFO priority (configurable for future pro-rata/parity)
- Market, Limit, IOC, FOK, GTC, GTD, Day orders
- Self-trade prevention (cancel-newest)
- Market-order residual policy (cancel or collar)
- Integer-encoded prices and quantities (no floating point on hot path)
- Pre-allocated order pool (zero allocation on hot path)

### Connectivity
- **FIX 4.4** gateway with real tag-value parser (no QuickFIX dependency)
- **TLS v1.3** with mTLS support via transport abstraction layer
- Persistent multi-message FIX sessions
- FIX session persistence (sequence numbers survive restart)
- Bounded gap recovery (ResendRequest / SequenceReset with PossDupFlag)
- Persistent FIX message store (file-backed, survives restart)
- **SBE** binary codec (encode/decode)
- String and numeric ClOrdID support

### Risk Controls
- Fat-finger price/quantity/notional checks
- Per-session rate throttling
- Participant-level kill switch
- Cancel-on-disconnect
- Configurable per-instrument price bands

### Operations
- **Web Portal** — 9-page ops dashboard with admin CRUD
- **RBAC** — viewer / operator / admin roles, per-endpoint enforcement
- **Metrics** — Prometheus text format + JSON export
- **Audit Log** — append-only JSON-line file, all mutating actions logged
- Instrument halt / resume / close via authenticated API
- EOD processing with auto-trigger on session close
- Session scheduler with automatic state transitions

### Durability & Recovery
- **DirectIOJournal** — production-grade with O_DIRECT (Linux) / F_NOCACHE (macOS)
- Segmented journal with rotation and CRC32 checksums
- Snapshot capture and restore
- Warm restart (snapshot + journal replay) and cold start (full replay)
- Startup controller with post-replay state validation

### DR / Failover
- **Journal replication** — synchronous mode, zero RPO
- **Automated failure detection** — health probe with state machine (HEALTHY → DEGRADED → FAILED)
- **Automated promotion** — safety-validated with generation-counter fencing
- **Split-brain protection** — stale primary rejected by generation check
- Simulated failover proven end-to-end in tests

### Deployment
- **Docker** — multi-stage build, Nginx portal, persistent volumes
- **Systemd** — service unit with restart policy and security hardening
- **JSON config** — instruments, sessions, participants loaded from files
- Pilot VM playbook (Hetzner CX23, 45 min from zero to validated)

## Project Structure

```
MatchingEngine/
├── engine/core/          # Matching engine kernel (order book, pool, matching, journal, snapshot)
├── gateway/fix/          # FIX 4.4 gateway (parser, adapter, TCP/TLS acceptor, session store, gap handler, message store)
├── gateway/sbe/          # SBE binary codec
├── risk/                 # Pre-trade risk service + rate limiter
├── market-data/          # Market data publisher + TCP transport
├── dropcopy/             # Drop copy service
├── refdata/              # Reference data service + HTTP handler
├── ops/                  # Operations controller, auth, RBAC, metrics, audit log
├── eod/                  # End-of-day processor + file writer
├── common/               # Transport abstraction (TCP/TLS), JSON config loader
├── replication/          # Journal replicator, receiver, health probe, failover controller, auto-promote
├── certification/        # Client certification harness + report writer
├── daemon/               # Standalone daemon entry point + integration tests
├── portal/               # Web-based ops dashboard (single-file HTML)
├── deploy/               # Docker config, pilot-env, systemd, scripts
├── docs/                 # Specifications (33), pilot docs (23), runbooks (7), technical design, delivery artifacts
├── Dockerfile            # Multi-stage build
├── docker-compose.yml    # Full stack (engine + portal)
└── benchmark/            # Prototype matching benchmark
```

## Quick Start

### Build from Source

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
ctest --timeout 30    # 444 tests
```

**Requirements:** C++20 compiler, CMake 3.20+, OpenSSL (for TLS)

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

## Configuration

### Platform (`deploy/docker-config/platform.conf`)

```ini
partition.id=1
partition.pool_capacity=100000
journal.type=segmented          # or direct_io
journal.flush_mode=sync         # Regulatory-Grade durability
fix.listen_port=9876
tls.enabled=true
tls.cert_file=/path/to/server.crt
tls.key_file=/path/to/server.key
ops.listen_port=8080
ops.api_key=your-secret-key
```

### Instruments (`deploy/docker-config/instruments.json`)

```json
{
  "instruments": [
    {
      "instrument_id": 100,
      "symbol": "BTC-USD",
      "tick_size": 1,
      "lot_size": 1,
      "static_ref_price": 50000,
      "static_band_pct": 10.0,
      "residual_policy": "cancel",
      "stp_mode": "cancel_newest"
    }
  ]
}
```

## Ports

| Port | Service | Protocol |
|:----:|---------|----------|
| 9876 | FIX Gateway | TCP/TLS (FIX 4.4) |
| 9877 | Market Data | TCP (JSON) |
| 8080 | Ops Console | HTTP |
| 80 | Portal | HTTP (Nginx) |

## Portal

Web-based operations dashboard at `http://localhost:80`.

| Section | Pages |
|---------|-------|
| Operations | Dashboard, Instruments (halt/resume), FIX Sessions, Kill Switch, Metrics, EOD, Audit Log |
| Admin | Instrument CRUD, Participant CRUD |

Login roles: `admin` (full access), `operator` (ops actions), `viewer` (read-only)

## API Endpoints

| Method | Path | Auth | Description |
|:------:|------|:----:|-------------|
| GET | `/api/v1/health` | Open | Platform health |
| GET | `/api/v1/instruments` | Open | List instruments |
| GET | `/api/v1/metrics` | Open | Prometheus metrics |
| GET | `/api/v1/metrics/json` | Open | JSON metrics |
| POST | `/api/v1/auth/login` | Open | JWT login |
| GET | `/api/v1/auth/me` | Open | Current user info |
| GET | `/api/v1/fix/sessions` | Open | FIX session list |
| GET | `/api/v1/eod/status` | Open | Last EOD result |
| GET | `/api/v1/audit` | Open | Audit log entries |
| GET | `/api/v1/kill-switch` | Open | Kill switch status |
| POST | `/api/v1/instruments/{id}/halt` | Operator | Halt instrument |
| POST | `/api/v1/instruments/{id}/resume` | Operator | Resume instrument |
| POST | `/api/v1/eod/trigger` | Operator | Trigger EOD |
| POST | `/api/v1/kill-switch/{pid}/activate` | Operator | Activate kill switch |
| POST | `/api/v1/instruments` | Admin | Create instrument |
| POST | `/api/v1/participants` | Admin | Create participant |

## Testing

```bash
cd build
ctest --timeout 30                           # All 444 tests
ctest -R DaemonFix                           # Daemon integration (19 tests)
ctest -R Lifecycle                           # Full lifecycle regression (2 tests)
ctest -R Replication                         # DR replication (8 tests)
ctest -R AutoPromote                         # Failover automation (11 tests)
ctest -R Mtls                                # mTLS (3 tests)
```

## Documentation

| Category | Count | Location |
|----------|:-----:|----------|
| Platform Specifications | 33 | `docs/specifications/` |
| Pilot Documents | 23 | `docs/pilot/` |
| Operational Runbooks | 7 | `docs/runbooks/` |
| Technical Design | 25+ | `docs/technical/` |
| Delivery Artifacts | 10+ | `docs/delivery/` |
| Architecture Diagrams | 12 | `docs/diagrams/` |

## Development History

Built iteratively across 30+ milestones:

- **M1–M9**: Core engine, gateways, risk, market data, EOD
- **M10–M11**: Segmented journal, startup controller, certification harness
- **M12–M14**: Daemon, TLS, session persistence, gap recovery, auth, metrics
- **M15–M17**: Pilot documentation, deployment packaging, evidence capture
- **Phase 2A**: Auto EOD, TOB quantity, FIX multi-message sessions
- **Phase 2B**: mTLS, DirectIO journal, persistent message store, FIX replay, RBAC, DR replication
- **Phase 2C**: Docker containerization, participant reconnection, automated failover

## Architecture Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Hot-path language | C++ | Latency + determinism |
| Partition model | Single-threaded per partition | No locks, provable determinism |
| Sequencing | Partition-local monotonic | No global sequencer bottleneck |
| Journal | Custom append-only with Direct I/O | Best alignment with C++ hot path |
| FIX implementation | Custom parser (no QuickFIX) | Full control, no external dependency |
| Price encoding | Integer (no floating point) | Deterministic matching |
| Replication | Synchronous journal replication | Zero RPO for Regulatory-Grade |
| Failover | Automated detection + promotion | Generation-counter fencing for split-brain |

## License

Proprietary. All rights reserved.
