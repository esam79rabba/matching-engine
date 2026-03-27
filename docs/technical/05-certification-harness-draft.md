# Certification Test Harness — Draft Architecture

**Status**: Starter artifact — decision-independent
**Source**: §23 Testing, Certification, and Release Management (frozen baseline)

---

## Purpose

Provide a self-service certification environment where participants can test connectivity, order lifecycle, and protocol compliance before production onboarding.

## Harness Components

```
Participant           Cert Environment
Test Client  ──────→  FIX/Binary Gateway (cert instance)
                         │
                         ▼
                      Risk Service (cert limits)
                         │
                         ▼
                      Matching Engine (cert partition)
                         │
                         ├──→ Market Data (cert channel)
                         ├──→ Drop Copy (cert session)
                         └──→ Test Result Validator
                                │
                                ▼
                          Certification Report
```

## 25 Mandatory Test Cases (Elaborated)

### Connectivity (CERT-001 to CERT-004)

| ID | Test | Steps | Expected Result | Auto-Validate? |
|----|------|-------|-----------------|:--------------:|
| CERT-001 | Valid logon | Send Logon with correct CompID + credentials | Logon accepted; session established | Yes |
| CERT-002 | Invalid logon | Send Logon with wrong credentials | Logon rejected; connection closed | Yes |
| CERT-003 | Heartbeat exchange | Wait for heartbeat interval; verify heartbeat sent/received | Session maintained | Yes |
| CERT-004 | Sequence gap recovery | Inject gap; verify ResendRequest; fill gap | Sequence restored; no message loss | Yes |

### Order Entry (CERT-005 to CERT-018)

| ID | Test | Steps | Expected Result | Auto-Validate? |
|----|------|-------|-----------------|:--------------:|
| CERT-005 | Limit buy | Submit limit buy; check ack | ExecutionReport(New) with venue OrderID | Yes |
| CERT-006 | Limit sell | Submit limit sell; check ack | ExecutionReport(New) | Yes |
| CERT-007 | Market order | Submit market; check fill or residual cancel | Fill at best price; residual cancelled (default) or per policy | Yes |
| CERT-008 | Modify price | Submit limit; modify price | ExecutionReport(Modify) confirmed | Yes |
| CERT-009 | Modify quantity | Submit limit; reduce qty | Confirmed; priority retained | Yes |
| CERT-010 | Cancel order | Submit limit; cancel | ExecutionReport(Cancel) | Yes |
| CERT-011 | IOC order | Submit IOC; partial fill expected | Partial fill + residual cancel | Yes |
| CERT-012 | FOK rejected | Submit FOK with qty > available | Rejected (insufficient liquidity) | Yes |
| CERT-013 | Fill notification | Submit aggressive order matching resting | ExecutionReport(Trade) with correct price/qty | Yes |
| CERT-014 | Partial fill | Submit order partially matching | CumQty and LeavesQty correct | Yes |
| CERT-015 | Invalid instrument | Submit order for non-existent instrument | Reject with code 1 (unknown instrument) | Yes |
| CERT-016 | Invalid price | Submit order with price not aligned to tick | Reject with code 5 (invalid tick) | Yes |
| CERT-017 | Price band violation | Submit order outside price bands | Reject with code 3 (outside bands) | Yes |
| CERT-018 | Rate limit | Submit orders exceeding rate limit | Reject with code 8 (throttled) | Yes |

### Disconnect / Recovery (CERT-019 to CERT-022)

| ID | Test | Steps | Expected Result | Auto-Validate? |
|----|------|-------|-----------------|:--------------:|
| CERT-019 | Graceful logout | Send Logout | Session closed cleanly | Yes |
| CERT-020 | Cancel-on-disconnect | Disconnect abruptly; verify CoD | All resting orders cancelled; cancel confirms on reconnect | Semi (needs reconnect) |
| CERT-021 | Drop copy completeness | Execute trades; verify all events on drop copy | All order/trade events present on DC session | Yes |
| CERT-022 | Mass cancel | Submit mass cancel request | All resting orders cancelled; count confirmed | Yes |

### Auction (CERT-023 to CERT-025)

| ID | Test | Steps | Expected Result | Auto-Validate? |
|----|------|-------|-----------------|:--------------:|
| CERT-023 | Pre-open order | Submit during pre-open | Accepted; no match | Yes |
| CERT-024 | Auction fill | Submit during auction; verify fill at equilibrium | Fill at single equilibrium price | Yes |
| CERT-025 | Market data depth | Subscribe; verify depth matches submitted orders | Depth messages match book state | Yes |

## Harness Design Principles

- Cert environment is isolated from production (separate instances, separate data)
- Cert engine runs a dedicated partition with synthetic instruments
- Counterparty orders pre-seeded by the harness (passive liquidity for testing)
- Each test case is self-contained and can be run independently
- Test results automatically validated; certification report generated
- Participant can re-run tests as needed; final certification requires all 25 passing

## Certification Report Format

```
Participant: [FirmName]
Session: [CompID]
Date: [YYYY-MM-DD]
Protocol: FIX 4.4 / Binary SBE

Test Results:
  CERT-001: PASS
  CERT-002: PASS
  ...
  CERT-025: PASS

Overall: 25/25 PASSED — CERTIFIED
Certified by: [Connectivity Team Lead]
Valid until: [Date + 12 months or next major protocol change]
```
