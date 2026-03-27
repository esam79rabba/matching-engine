# Runbook: Platform Startup

**Audience**: Operator / Engineer
**Last updated**: 2026-03-25
**Applies to**: FR-1 prototype pilot

---

## Prerequisites

- Build completed: `cd build && cmake .. && make -j$(nproc)`
- All tests passing: `cd build && ctest --output-on-failure`
- Config files present in `deploy/config/`:
  - `platform.conf` — platform settings
  - `instruments.json` — instrument definitions
  - `sessions.json` — session profiles and participant config

## Startup Procedure

### 1. Create data directories

```bash
mkdir -p deploy/data/{journal,snapshots,eod}
mkdir -p deploy/logs deploy/run
```

### 2. Run smoke tests

```bash
cd build && ctest --output-on-failure
```

All 253+ tests must pass before proceeding.

### 3. Start platform

```bash
cd deploy/scripts && ./start.sh
```

The script will:
1. Verify config files exist
2. Check build artifacts
3. Run smoke tests
4. Display port assignments

### 4. Verify readiness

- FIX port 9876 — for participant connections
- Market data port 9877 — for market data subscribers
- Ops HTTP port 8080 — for operations console

### 5. Systemd Service (Managed Runtime)

For managed deployment with automatic restart:

```bash
# Install (requires root)
sudo deploy/systemd/install.sh

# Enable and start
sudo systemctl enable matching-engine
sudo systemctl start matching-engine

# Check status
sudo systemctl status matching-engine

# View logs
sudo journalctl -u matching-engine -f
```

See `deploy/systemd/matching-engine.service` for the full service unit configuration.

### 6. Direct Daemon (Development)

For development/testing without systemd:

```bash
cd build
./daemon/matching-engine-daemon ../deploy/config/platform.conf
```

Press Ctrl+C for graceful shutdown.

## Expected Outcome

- All smoke tests pass
- Port assignments displayed
- Platform ready for certification harness or integration testing

## Failure Recovery

| Symptom | Cause | Action |
|---------|-------|--------|
| Smoke tests fail | Build issue or test regression | Re-run `cmake .. && make -j` then `ctest` |
| Config file missing | Wrong config directory | Verify `deploy/config/` contents |
| Port already in use | Previous instance not stopped | Run `deploy/scripts/stop.sh` first |
