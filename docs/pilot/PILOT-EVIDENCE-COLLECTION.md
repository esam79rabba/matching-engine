# Pilot Evidence Collection Guide

**Version**: 1.0 | **Date**: 2026-03-25
**Purpose**: Ensure all pilot artifacts are captured for post-pilot analysis
**When**: During and immediately after pilot execution

---

## Evidence Directory Structure

```
pilot-evidence-YYYYMMDD/
├── config/                  # Config files used during pilot
│   ├── platform.conf
│   ├── instruments.json
│   └── sessions.json
├── connectivity/            # Connectivity validation results
│   └── connectivity-report.txt
├── certification/           # Certification harness results
│   ├── cert_report.json
│   └── cert_report.csv
├── metrics/                 # Metrics snapshots
│   ├── metrics_pre_market.txt
│   ├── metrics_during_trading.txt
│   └── metrics_post_market.txt
├── eod/                     # End-of-day output files
│   ├── trade_summary_YYYYMMDD.csv
│   ├── fee_summary_YYYYMMDD.csv
│   └── daily_report_YYYYMMDD.txt
├── logs/                    # Platform logs
│   └── daemon_YYYYMMDD.log
├── fix-sessions/            # FIX session state
│   └── sessions.dat
├── issues/                  # Issue log
│   └── PILOT-ISSUE-LOG.md
├── screenshots/             # Manual screenshots if any
└── notes/                   # Operator notes
    └── operator_notes.md
```

---

## Collection Checklist

### Before Market Open

| # | Evidence | Command / Action | Collected |
|:-:|----------|-----------------|:---------:|
| 1 | Config snapshot | `cp config/* pilot-evidence/config/` | |
| 2 | Connectivity report | `./validate-connectivity.sh > pilot-evidence/connectivity/` | |
| 3 | Pre-market metrics | `curl localhost:8080/api/v1/metrics > pilot-evidence/metrics/metrics_pre_market.txt` | |
| 4 | Certification report | Run cert harness, copy JSON/CSV output | |

### During Trading

| # | Evidence | Command / Action | Collected |
|:-:|----------|-----------------|:---------:|
| 5 | Periodic metrics | `curl localhost:8080/api/v1/metrics > pilot-evidence/metrics/metrics_during_trading.txt` | |
| 6 | Issue log maintained | Keep PILOT-ISSUE-LOG.md updated in real-time | |
| 7 | Operator notes | Record observations in `notes/operator_notes.md` | |

### After Market Close

| # | Evidence | Command / Action | Collected |
|:-:|----------|-----------------|:---------:|
| 8 | Final metrics | `curl localhost:8080/api/v1/metrics > pilot-evidence/metrics/metrics_post_market.txt` | |
| 9 | EOD files | `cp data/eod/* pilot-evidence/eod/` | |
| 10 | FIX session state | `cp data/fix-sessions/* pilot-evidence/fix-sessions/` | |
| 11 | Platform logs | `journalctl -u matching-engine --since "today" > pilot-evidence/logs/daemon_$(date +%Y%m%d).log` | |
| 12 | Issue log finalized | Copy completed issue log to `pilot-evidence/issues/` | |

### Post-Pilot

| # | Evidence | Action | Collected |
|:-:|----------|--------|:---------:|
| 13 | Participant feedback | Request written feedback from participant | |
| 14 | Post-pilot report | Complete POST-PILOT-REPORT-TEMPLATE.md | |
| 15 | Archive | Compress and store `pilot-evidence-YYYYMMDD/` | |

---

## Naming Convention

| Type | Pattern | Example |
|------|---------|---------|
| Directory | `pilot-evidence-YYYYMMDD` | `pilot-evidence-20260401` |
| Metrics | `metrics_{phase}.txt` | `metrics_pre_market.txt` |
| Logs | `daemon_YYYYMMDD.log` | `daemon_20260401.log` |
| EOD | `{type}_YYYYMMDD.csv` | `trade_summary_20260401.csv` |
| Screenshots | `{description}_HHMMSS.png` | `logon_success_091502.png` |

---

## Evidence Collection Script

```bash
#!/usr/bin/env bash
# Quick evidence collection after pilot
DATE=$(date +%Y%m%d)
DIR="pilot-evidence-${DATE}"

mkdir -p "${DIR}"/{config,connectivity,certification,metrics,eod,logs,fix-sessions,issues,notes}

# Config
cp /opt/matching-engine/config/{platform.conf,instruments.json,sessions.json} "${DIR}/config/" 2>/dev/null

# Metrics
curl -s localhost:8080/api/v1/metrics > "${DIR}/metrics/metrics_final.txt" 2>/dev/null

# EOD
cp /opt/matching-engine/data/eod/* "${DIR}/eod/" 2>/dev/null

# Logs
journalctl -u matching-engine --since "today" > "${DIR}/logs/daemon_${DATE}.log" 2>/dev/null

# FIX sessions
cp /opt/matching-engine/data/fix-sessions/* "${DIR}/fix-sessions/" 2>/dev/null

echo "Evidence collected in ${DIR}/"
ls -la "${DIR}/"
```

---

## Retention

- Keep pilot evidence for at least **12 months**
- Store in a location accessible to engineering, operations, and compliance
- If regulatory review is expected, retain per ADGM record-keeping requirements
