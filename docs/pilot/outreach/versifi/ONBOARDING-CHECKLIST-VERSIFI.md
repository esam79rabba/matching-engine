# Onboarding Package Checklist — VersiFi

| # | Item | Source | Sent | Confirmed |
|:-:|------|--------|:----:|:---------:|
| 1 | Participant onboarding guide | `docs/pilot/PARTICIPANT-ONBOARDING.md` | | |
| 2 | Server TLS certificate | `deploy/pilot-env/tls/server.crt` | | |
| 3 | Connection details sheet | `outreach/versifi/CONNECTION-DETAILS-VERSIFI.md` | | |
| 4 | Limitations acknowledged | Section 11 of onboarding guide | | |

## VersiFi-Specific Configuration

| Item | Value | Applied |
|------|-------|:-------:|
| SenderCompID | VERSIFI | |
| participant_id | 3 | |
| Risk: max_order_rate | 500 | |
| Risk: fat_finger_price_pct | 10.0 | |
| Risk: fat_finger_max_notional | 5000000 | |
| sessions.json updated | | |
| Firewall rule for VersiFi IP | | |

## Pre-Send Checks

| # | Check | Done |
|:-:|-------|:----:|
| 1 | Connection details sheet host IP filled in | |
| 2 | Operator contact filled in | |
| 3 | CompID added to sessions.json | |
| 4 | Daemon tested with VERSIFI CompID | |
| 5 | Package sent via secure channel | |
| 6 | Receipt confirmed by VersiFi | |
