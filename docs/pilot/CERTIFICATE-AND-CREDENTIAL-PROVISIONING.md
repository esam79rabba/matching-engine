# Certificate and Credential Provisioning Guide

**Version**: 1.0 | **Date**: 2026-03-25
**Applies to**: FR-1 prototype pilot
**Audience**: Venue operator, security team

---

## 1. Overview

This guide covers provisioning all credentials needed for a controlled pilot:

| Credential | Purpose | Who Provisions |
|-----------|---------|----------------|
| Server TLS certificate | Encrypt FIX connections | Venue operator |
| Participant CompID | FIX session identity | Venue operator |
| Ops API key | Protect ops console endpoints | Venue operator |
| Market data access | No auth required (prototype) | N/A |

**Prototype limitation**: This is not a production PKI/IAM system. All credential management is manual and file-based.

---

## 2. Server TLS Certificates

### 2.1 Generate Self-Signed Certificate (Pilot Only)

For pilot use, generate a self-signed certificate:

```bash
# Create TLS directory
mkdir -p /opt/matching-engine/config/tls

# Generate private key
openssl genrsa -out /opt/matching-engine/config/tls/server.key 2048

# Generate self-signed certificate (valid 365 days)
openssl req -new -x509 \
    -key /opt/matching-engine/config/tls/server.key \
    -out /opt/matching-engine/config/tls/server.crt \
    -days 365 \
    -subj "/CN=matching-engine-pilot/O=VenueOps/C=AE"

# Verify certificate
openssl x509 -in /opt/matching-engine/config/tls/server.crt -text -noout

# Set permissions
chmod 600 /opt/matching-engine/config/tls/server.key
chmod 644 /opt/matching-engine/config/tls/server.crt
```

### 2.2 Enable TLS in Platform Config

Edit `config/platform.conf`:
```
tls.enabled=true
tls.cert_file=/opt/matching-engine/config/tls/server.crt
tls.key_file=/opt/matching-engine/config/tls/server.key
```

### 2.3 Distribute CA Certificate to Participants

Provide participants with the server certificate (or CA cert if using CA-signed):
```bash
# Copy server cert for participant distribution
cp /opt/matching-engine/config/tls/server.crt ./participant-bundle/venue-ca.crt
```

Participants must configure their FIX client to trust this certificate.

### 2.4 mTLS (Optional, Not Required for Pilot)

For mutual TLS (participants present client certificates):

```bash
# Generate CA for client certs
openssl genrsa -out config/tls/ca.key 2048
openssl req -new -x509 -key config/tls/ca.key -out config/tls/ca.crt \
    -days 365 -subj "/CN=Venue-Pilot-CA/O=VenueOps"

# Generate participant client cert
openssl genrsa -out participant-alpha.key 2048
openssl req -new -key participant-alpha.key \
    -out participant-alpha.csr \
    -subj "/CN=MM_ALPHA/O=MarketMakerAlpha"
openssl x509 -req -in participant-alpha.csr \
    -CA config/tls/ca.crt -CAkey config/tls/ca.key \
    -CAcreateserial -out participant-alpha.crt -days 365

# Enable mTLS in config
# tls.ca_file=/opt/matching-engine/config/tls/ca.crt
# tls.require_client_cert=true
```

**Status**: mTLS is supported by the transport layer but not required for pilot.

### 2.5 Production Expectations (Not Pilot)

| Area | Pilot | Production |
|------|-------|-----------|
| Certificate authority | Self-signed | Proper CA (internal or commercial) |
| Key storage | File on disk | HSM or secrets manager |
| Rotation | Manual, annual | Automated, 90-day |
| Revocation | Not implemented | CRL or OCSP |
| Monitoring | Manual check | Certificate expiry alerting |

---

## 3. Participant CompID Provisioning

### 3.1 Assign CompID

Each participant needs a unique SenderCompID. Add to `config/sessions.json`:

```json
{
  "participants": [
    {
      "participant_id": 1,
      "name": "Market Maker Alpha",
      "sender_comp_id": "MM_ALPHA",
      "max_order_rate": 1000,
      "fat_finger_price_pct": 10.0,
      "fat_finger_max_notional": 10000000
    }
  ]
}
```

### 3.2 CompID Naming Convention

| Format | Example | Notes |
|--------|---------|-------|
| `MM_<NAME>` | MM_ALPHA | Market makers |
| `BRK_<NAME>` | BRK_BETA | Brokers |
| `PROP_<NAME>` | PROP_GAMMA | Proprietary traders |
| `TEST_<NAME>` | TEST_SANDBOX | Test/sandbox accounts |

### 3.3 Communicate to Participant

Provide participant with:
- SenderCompID (e.g., `MM_ALPHA`)
- TargetCompID: `VENUE`
- FIX host and port
- Server CA certificate (if TLS enabled)
- Heartbeat interval: 30 seconds
- Reference to the [Participant Onboarding Guide](PARTICIPANT-ONBOARDING.md)

---

## 4. Ops Console API Key

### 4.1 Generate API Key

```bash
# Generate a random 32-character API key
API_KEY=$(openssl rand -hex 16)
echo "Generated API key: ${API_KEY}"

# Store in environment file
echo "OPS_API_KEY=${API_KEY}" >> /opt/matching-engine/config/environment
```

### 4.2 Configure in Daemon

The daemon reads the API key from the environment file and configures the auth middleware. The current prototype requires wiring the key in the daemon's startup code.

### 4.3 Distribute to Authorized Operators

Provide the API key only to authorized operations staff. Include in ops requests:
```bash
curl -H "Authorization: Bearer ${API_KEY}" http://localhost:8080/api/v1/instruments/100/halt
# or
curl -H "X-API-Key: ${API_KEY}" http://localhost:8080/api/v1/instruments/100/halt
```

### 4.4 Key Rotation

For the prototype pilot:
1. Generate a new key
2. Update the environment file
3. Restart the daemon
4. Distribute the new key to operators

**Production**: API key management would use a secrets manager with automatic rotation.

---

## 5. Credential Inventory Template

| Credential | Value | Holder | Provisioned | Expires |
|-----------|-------|--------|:-----------:|:-------:|
| Server TLS cert | server.crt | Venue | | |
| Server TLS key | server.key | Venue (restricted) | | |
| Ops API key | (secret) | Ops team | | N/A |
| MM_ALPHA CompID | MM_ALPHA | Participant Alpha | | N/A |
| BRK_BETA CompID | BRK_BETA | Participant Beta | | N/A |

---

## 6. Security Notes

- **This is prototype-grade credentialing.** Production requires HSM key storage, automated rotation, and proper IAM.
- Self-signed certificates are for pilot only. Production requires CA-signed certificates.
- API keys are stored in plaintext environment files. Production requires a secrets manager.
- CompID assignment is manual. Production requires an onboarding workflow with approval.
- No credential audit logging in the prototype. Production requires full audit trail.
