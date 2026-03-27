# Pilot Deployment Guide

**Version**: 2.0 | **Date**: 2026-03-25
**Applies to**: FR-1 prototype

---

## Overview

This guide enables an engineer or operator to build, configure, and exercise the matching engine prototype without reading source code.

## Quick Start

```bash
# 1. Build
cd build
cmake ..
make -j$(nproc)

# 2. Run all tests
ctest --output-on-failure
# Expected: 270+ tests, 100% passing

# 3. Run certification harness
./certification/cert-harness-tests

# 4. Start daemon
./daemon/matching-engine-daemon ../deploy/config/platform.conf

# Or use the start script (runs smoke tests first):
cd ../deploy/scripts && ./start.sh
```

## Directory Layout

```
deploy/
├── config/
│   ├── platform.conf        # Platform configuration
│   ├── instruments.json     # Instrument definitions (BTC-USD, ETH-USD)
│   └── sessions.json        # Session profiles, participant config
├── scripts/
│   ├── start.sh             # Startup with smoke tests
│   ├── stop.sh              # Graceful shutdown
│   └── warm-restart.sh      # Warm restart (snapshot + replay)
├── data/                    # Created at runtime
│   ├── journal/             # Segmented journal files
│   ├── snapshots/           # Partition snapshots
│   └── eod/                 # End-of-day reports
├── logs/                    # Smoke test and runtime logs
├── run/                     # PID files (when daemon mode available)
└── PILOT-GUIDE.md           # This file
```

## Network Ports

| Port | Service | Protocol | Purpose |
|:----:|---------|----------|---------|
| 9876 | FIX Gateway | TCP (FIX 4.4) | Order entry, cancel, modify |
| 9877 | Market Data | TCP (JSON) | Trade and book updates |
| 8080 | Operations | HTTP | Health, halt/resume, kill switch, ref data |

## Configuration

### Instruments (`config/instruments.json`)

Each instrument defines:
- `instrument_id` — unique numeric ID
- `symbol` — human-readable (e.g., "BTC-USD")
- `tick_size`, `lot_size` — minimum price/quantity increments
- `static_ref_price` — reference price for price band checks
- `static_band_pct` — maximum deviation from reference (%)
- `residual_policy` — what happens to unmatched market-order remainder ("cancel" or "collar")
- `stp_mode` — self-trade prevention mode ("cancel_newest")

### Sessions (`config/sessions.json`)

Defines:
- Session schedule (PRE_OPEN → CONTINUOUS → CLOSED times)
- Participant credentials (sender_comp_id, risk limits)
- Rate limits per session

### Platform (`config/platform.conf`)

Key settings:
- `journal.flush_mode` — "sync" (Regulatory-Grade) or "batch" (Reduced-Latency)
- `partition.pool_capacity` — max concurrent orders
- `fix.session_store_path` — FIX session state persistence file
- Port assignments for all services
- TLS config placeholders (commented out; enable when certificates are provisioned)

## Operational Runbooks

| Runbook | Location |
|---------|----------|
| Startup | `docs/runbooks/01-startup.md` |
| Shutdown | `docs/runbooks/02-shutdown.md` |
| Warm Restart | `docs/runbooks/03-warm-restart.md` |
| Halt / Resume | `docs/runbooks/04-halt-resume.md` |
| Kill Switch | `docs/runbooks/05-kill-switch.md` |
| EOD Processing | `docs/runbooks/06-eod-processing.md` |
| Troubleshooting | `docs/runbooks/07-troubleshooting.md` |

## Current Limitations

| Area | Current State | Production Requirement |
|------|---------------|----------------------|
| Daemon mode | Standalone daemon with signal handling | Full process supervisor / systemd |
| TLS | OpenSSL TLS on FIX transport (when certs provided) | mTLS on all connections |
| FIX session persistence | Sequence numbers survive restart | Full session recovery with gap fill |
| Auth/RBAC | Not implemented | Role-based access on ops console |
| Multicast MD | TCP/JSON only | UDP multicast with SBE encoding |
| DR | Single-site warm restart only | Journal replication to DR site |
| Monitoring | No metrics export | Prometheus metrics, Grafana dashboards |
| FIX gap recovery | Not implemented | ResendRequest/SequenceReset |

## Prototype Performance (Not Production Certification)

| Metric | Result | Caveat |
|--------|--------|--------|
| Matching insert p99 | ~3.9 us | Dev hardware, no CPU pinning |
| Matching match p99 | ~2.5 us | Dev hardware, no CPU pinning |
| Stress throughput | ~537K msg/s | Single partition, dev hardware |
| Test suite | ~1.7 seconds | 253 tests |

These are directional indicators only. Production performance requires colocated hardware with CPU pinning, NVMe Direct I/O, and DPDK networking.
