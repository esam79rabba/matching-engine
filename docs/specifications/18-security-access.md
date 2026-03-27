# 18. Security and Access Control Requirements

## 18.1 Security Architecture

```
                          ┌─────────────────────┐
                          │  INTERNET / WAN      │
                          └─────────┬───────────┘
                                    │
                          ┌─────────▼───────────┐
                          │  DDoS MITIGATION     │
                          │  (network appliance)  │
                          └─────────┬───────────┘
                                    │
                  ┌─────────────────▼─────────────────┐
                  │        DMZ / GATEWAY ZONE           │
                  │  ┌─────────┐  ┌─────────┐          │
                  │  │ FIX GW  │  │ WS/REST │          │
                  │  └────┬────┘  └────┬────┘          │
                  │       │            │                │
                  │  ┌────▼────────────▼────┐          │
                  │  │   FIREWALL / WAF      │          │
                  │  └──────────┬───────────┘          │
                  └─────────────┼───────────────────────┘
                                │
                  ┌─────────────▼───────────────────────┐
                  │       APPLICATION ZONE               │
                  │  ┌──────┐ ┌──────┐ ┌──────────────┐ │
                  │  │ Risk │ │ Ref  │ │ Ops Console  │ │
                  │  │ Svc  │ │ Data │ │ (behind SSO) │ │
                  │  └──┬───┘ └──────┘ └──────────────┘ │
                  └─────┼───────────────────────────────┘
                        │
                  ┌─────▼───────────────────────────────┐
                  │       MATCHING ENGINE ZONE            │
                  │  ┌──────────┐  ┌──────────────────┐  │
                  │  │Sequencer │  │ Matching Engine   │  │
                  │  └──────────┘  └──────────────────┘  │
                  │  ┌──────────┐  ┌──────────────────┐  │
                  │  │ Journal  │  │ Snapshots         │  │
                  │  └──────────┘  └──────────────────┘  │
                  └──────────────────────────────────────┘
                                │
                  ┌─────────────▼───────────────────────┐
                  │       DATA ZONE                      │
                  │  ┌──────────┐  ┌──────────────────┐  │
                  │  │ Audit DB │  │ Reference DB      │  │
                  │  └──────────┘  └──────────────────┘  │
                  └──────────────────────────────────────┘
```

## 18.2 Access Control

### 18.2.1 RBAC Model

| Role | Scope | Permissions |
|------|-------|-------------|
| **Market Operator** | Operations | Session control, instrument halt/resume, parameter view |
| **Senior Market Operator** | Operations | All Market Operator + parameter changes, trade bust (second approver) |
| **Surveillance Officer** | Compliance | Surveillance dashboard, audit trail query, order reconstruction |
| **Compliance Officer** | Compliance | Regulatory reports, data exports, policy management |
| **Risk Manager** | Risk | Risk limit configuration, kill switch, exposure dashboards |
| **System Administrator** | Infrastructure | Infrastructure management, deployment, monitoring |
| **Security Administrator** | Security | Access control, certificate management, security logs |
| **Support Analyst** | Support | Read-only access to participant data, order/trade inquiry |
| **Participant Admin** | Participant portal | Own firm's sessions, users, API keys, reports |
| **Platform Admin** | Full | All permissions (break-glass only; requires approval) |

### 18.2.2 Authentication Methods

| Interface | Method | Details |
|-----------|--------|---------|
| FIX gateway | Session credentials + mTLS | CompID/password in Logon + client certificate |
| Binary gateway | mTLS + session token | Client certificate + token exchange at logon |
| WebSocket/REST | API key + HMAC / OAuth 2.0 | HMAC-signed requests or bearer token |
| Operations console | SSO (SAML/OIDC) + MFA | Corporate SSO integration with hardware MFA token |
| Admin API | OAuth 2.0 + mTLS | Machine-to-machine authentication |
| Inter-service | mTLS | Mutual TLS between all internal services |

### 18.2.3 Certificate Management

| Requirement | Specification |
|-------------|--------------|
| CA | Private CA for internal services; public CA for participant-facing endpoints |
| Certificate lifetime | 1 year for services; configurable for participants |
| Rotation | Automated rotation with zero-downtime (dual-cert support) |
| Revocation | CRL or OCSP for certificate revocation |
| HSM | Private keys for CA and critical services stored in HSM |
| Audit | All certificate issuance, rotation, and revocation logged |

## 18.3 Data Protection

| Requirement | Specification |
|-------------|--------------|
| Encryption in transit | TLS 1.3 minimum for all non-colocation connections |
| Encryption at rest | AES-256 for journals, snapshots, databases, backups |
| Key management | HSM-backed key storage; key rotation policy (annual minimum) |
| Secrets management | Dedicated secrets manager (HashiCorp Vault or equivalent); no secrets in code or config files |
| PII protection | Participant PII encrypted at rest; access logged; GDPR/privacy compliance |
| Data classification | All data classified: PUBLIC, INTERNAL, CONFIDENTIAL, RESTRICTED |

## 18.4 Network Security

| Control | Specification |
|---------|--------------|
| Network segmentation | Separate zones: DMZ, Application, Matching Engine, Data (see §18.1) |
| Firewall rules | Whitelist-only between zones; deny-all default |
| DDoS protection | Network-level DDoS mitigation for internet-facing endpoints |
| IP whitelisting | Participant connections restricted to registered IP ranges |
| IDS/IPS | Intrusion detection/prevention on zone boundaries |
| VPN | Site-to-site VPN for DR replication; no public internet exposure for engine zone |

## 18.5 Operational Security

| Control | Specification |
|---------|--------------|
| Privileged access | Break-glass procedure; time-limited; fully audited |
| Production access | No direct developer access to production; all changes via CI/CD |
| Change management | All production changes require approval, testing, and rollback plan |
| Patch management | Security patches applied within SLA (critical: 24h; high: 7 days) |
| Vulnerability scanning | Automated in CI/CD; weekly production scans |
| Penetration testing | Annual third-party test; quarterly internal test |
| Incident response | Documented IR plan; tested quarterly; 24/7 on-call |
| Forensic readiness | Log retention and tooling for post-incident forensic analysis |
| Secure SDLC | Security review in design; SAST/DAST in CI; dependency scanning |
| Supply chain | Dependency vulnerability scanning; SBOM generation; signed artifacts |

## 18.6 Tamper-Evident Audit Trail

### 18.6.1 Hash Chain

```
Entry N:   [Payload_N] [Hash(Payload_N + Hash_N-1)] = Hash_N
Entry N+1: [Payload_N+1] [Hash(Payload_N+1 + Hash_N)] = Hash_N+1
```

- Each audit entry includes a cryptographic hash of its payload concatenated with the previous entry's hash
- Any modification to a historical entry breaks the hash chain
- Periodic anchor hashes published to an external timestamping service for independent verification
- Hash algorithm: SHA-256 minimum

### 18.6.2 Integrity Verification
- Automated daily integrity check: recompute hash chain from first entry
- Alert on any hash chain breakage
- External auditor can independently verify chain integrity
