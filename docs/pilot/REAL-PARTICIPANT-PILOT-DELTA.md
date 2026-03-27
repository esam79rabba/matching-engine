# Real-Participant Pilot Delta Summary

**Date**: 2026-03-26
**Baseline**: Hardened pilot 22/22 pass (Phase 2B complete)
**Current**: Phase 2C-M1 complete — containerized, reconnection proven, 423 tests

---

## What Changed Since Last Pilot

| Area | Hardened Pilot (Previous) | Containerized (Current) |
|------|--------------------------|------------------------|
| Deployment | Build from source, run daemon directly | **Docker image** — `docker-compose up` |
| Restart recovery | Session store + journal persist locally | **Docker volumes** — persist across container restart |
| Participant reconnect | Persisted seq numbers but untested | **Proven** — 3 reconnection tests, seq resumption verified |
| Config management | Edit files on host, restart daemon | **Mount config** from host into container |
| Health monitoring | curl manually | **Docker healthcheck** built in |
| Restart policy | Manual | **`restart: unless-stopped`** — auto-restart on crash |

## What the Participant Sees

- Same FIX 4.4 protocol, same TLS, same message format
- Same instruments (BTC-USD, ETH-USD)
- Same port assignments (9876 FIX, 9877 MD, 8080 Ops)
- **New**: if venue restarts, participant reconnects and sequence numbers resume
- **New**: venue runs as a managed container with health monitoring

## What the Participant Does NOT Need to Know

- Docker internals
- Volume management
- Build toolchain
- Journal type selection
