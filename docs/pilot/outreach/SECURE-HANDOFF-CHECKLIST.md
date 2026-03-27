# Secure Connection-Details Handoff Checklist

**Purpose**: Ensure credentials and connection details are shared securely.

---

## Before Handoff

| # | Check | Owner | Done |
|:-:|-------|-------|:----:|
| 1 | CompID assigned in sessions.json | Operations | |
| 2 | Risk limits configured for this participant | Operations | |
| 3 | TLS certificate generated (CA-signed preferred) | Security | |
| 4 | Certificate expiry > 90 days from pilot date | Security | |
| 5 | Connection details sheet prepared | Operations | |
| 6 | Onboarding guide reviewed for accuracy | Engineering | |
| 7 | Firewall rule submitted for participant IP range | Infrastructure | |

## During Handoff

| # | Step | Method | Done |
|:-:|------|--------|:----:|
| 8 | Send onboarding guide | Email (not sensitive) | |
| 9 | Send TLS certificate | Email or secure share (public cert, not secret) | |
| 10 | Send connection details sheet | Encrypted email or secure channel | |
| 11 | Confirm participant received all items | Email acknowledgment | |
| 12 | Record delivery date in operator notes | Internal | |

## After Handoff

| # | Check | Owner | Done |
|:-:|-------|-------|:----:|
| 13 | Participant confirms they can read the onboarding guide | Operations | |
| 14 | Participant confirms they have the TLS cert installed | Participant | |
| 15 | Participant confirms FIX client configured with CompID | Participant | |
| 16 | Schedule onboarding call | Operations | |

## What NOT to Share

- Ops API key (operator-only)
- Server private key
- Other participants' CompIDs or config
- Internal pilot execution docs
- Platform source code
- Unredacted daemon logs

## Credential Revocation (If Needed)

If the participant withdraws or the pilot is cancelled:
1. Remove CompID from sessions.json
2. Restart daemon to apply
3. Close firewall rule
4. No certificate revocation needed for self-signed (revoke if CA-signed)
