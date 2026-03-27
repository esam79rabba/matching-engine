# Security Architecture — Draft Outline

**Status**: Starter artifact — decision-independent
**Source**: §18 Security and Access Control (frozen baseline)

---

## 1. Network Zones

| Zone | Components | Access |
|------|-----------|--------|
| **DMZ** | FIX gateway, binary gateway, WS/REST gateway (Phase 2) | Participant connections (mTLS, IP whitelist) |
| **Application** | Risk service, reference data, ops console, post-trade | Internal services only; no direct participant access |
| **Engine** | Sequencer, matching engine, journal, output bus | Isolated; no external network; application zone access via defined interfaces |
| **Data** | Audit DB, reference DB, snapshots | Application zone access only; encrypted at rest |

**Rules**:
- No direct participant access to the Engine zone. Participants connect only to DMZ gateways.
- The Engine zone is reachable only through explicitly approved interfaces: gateway-to-sequencer ingress path (hot-path-optimized) and control-plane management interfaces.
- Hot-path ingress (gateway → risk → sequencer) uses dedicated low-latency interfaces, not general application-layer routing.
- Control-plane and general service traffic (ops console, reference data, monitoring) are separated from the hot path.
- All zone boundaries enforced by firewall rules with deny-all default.

## 2. Authentication Model

| Interface | Method | Details |
|-----------|--------|---------|
| FIX gateway | Session credentials + mTLS | CompID/password in Logon message + client X.509 certificate |
| Binary gateway | mTLS + session token | Client certificate + token exchange at logon |
| WS/REST gateway (Phase 2) | API key + HMAC signature OR OAuth 2.0 bearer token | Per-request signature or bearer token |
| Operations console | SSO (SAML/OIDC) + MFA | Corporate SSO with hardware MFA token |
| Admin API | OAuth 2.0 + mTLS | Machine-to-machine; service accounts |
| Inter-service | mTLS | Mutual TLS between all internal services |

## 3. PKI Design

| Element | Specification |
|---------|--------------|
| Root CA | Private CA; offline root; HSM-backed key |
| Issuing CA | Online issuing CA for service and participant certificates |
| Service certificates | 1-year lifetime; automated rotation with zero-downtime (dual-cert) |
| Participant certificates | Configurable lifetime; issued via participant admin portal |
| Revocation | CRL distribution point; OCSP responder for real-time checks |
| Key storage | HSM for CA keys and critical service keys (candidate vendors: Thales Luna, Entrust nShield, cloud HSM) |

## 4. RBAC Model

| Role | Scope | Key Permissions |
|------|-------|----------------|
| Market Operator | Operations | Session control, instrument halt/resume, parameter view |
| Senior Market Operator | Operations | + parameter changes, trade bust (second approver) |
| Surveillance Officer | Compliance | Audit trail query, order reconstruction, alerts |
| Risk Manager | Risk | Risk limit config, kill switch, exposure dashboards |
| System Administrator | Infrastructure | Deployment, monitoring, capacity management |
| Security Administrator | Security | Access control, certificate management, security logs |
| Support Analyst | Support | Read-only participant data, order/trade inquiry |
| Participant Admin | Self-service | Own firm sessions, users, API keys, reports |
| Platform Admin | Full | All permissions (break-glass only; requires approval) |

## 5. Secrets Management

| Approach | Candidate Tools |
|----------|----------------|
| Secrets store | HashiCorp Vault (candidate) or cloud-native (AWS Secrets Manager, Azure KV) |
| Secret injection | Environment variables or sidecar agent; never in code or config files |
| Rotation | Automated rotation for service credentials; manual + notified for participant |
| Audit | All secret access logged; all rotations logged |

## 6. Encryption

| Scope | Standard |
|-------|---------|
| In transit | TLS 1.3 minimum for all non-colocation connections |
| At rest | AES-256 for journal files, snapshots, databases, backups |
| Key management | HSM-backed; annual key rotation minimum |

## 7. Operational Security

| Control | Specification |
|---------|--------------|
| Production access | No direct developer access; all changes via CI/CD |
| Privileged access | Break-glass procedure; time-limited; fully audited |
| Patching | Critical: 24h; High: 7 days; automated scanning in CI/CD |
| Pen testing | Annual third-party; quarterly internal |
| SBOM | Generated per build; dependency vulnerability scanning |

## 8. FR-1 Security Scope

| Capability | FR-1 | Phase 2+ |
|-----------|:----:|:--------:|
| mTLS (all connections) | Yes | Yes |
| RBAC (ops console) | Basic (3 roles) | Full (9 roles) |
| HSM (CA keys) | Yes | Yes |
| Secrets manager | Yes | Yes |
| Encryption at rest | Yes | Yes |
| Network segmentation | Yes (3 zones) | Yes (4 zones) |
| Pen testing | Pre-launch | Annual |
| DDoS protection | Basic (IP whitelist) | Advanced (appliance) |

## Open Items

- HSM vendor selection: pending procurement evaluation
- SSO provider: depends on organization's existing identity infrastructure
- DDoS appliance: Phase 2 procurement
