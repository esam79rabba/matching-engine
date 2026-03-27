# Pilot Environment Configuration Pack

This directory contains a complete, coherent configuration set for running an FR-1 pilot.

## Files

| File | Purpose | Edit Required |
|------|---------|:-------------:|
| `platform.conf` | Core platform settings | Yes — paths, ports |
| `instruments.json` | Tradeable instruments | Yes — for your market |
| `sessions.json` | Session schedule + participants | Yes — participant details |
| `environment` | Secrets (API keys) | Yes — generate keys |
| `tls/README.md` | TLS certificate instructions | Follow steps |

## Quick Setup

```bash
# 1. Copy this directory to the install location
cp -r deploy/pilot-env /opt/matching-engine/config

# 2. Edit platform.conf paths to match your install
vim /opt/matching-engine/config/platform.conf

# 3. Add participant details to sessions.json
vim /opt/matching-engine/config/sessions.json

# 4. Generate and store API key
API_KEY=$(openssl rand -hex 16)
echo "OPS_API_KEY=${API_KEY}" > /opt/matching-engine/config/environment

# 5. (Optional) Generate TLS certificates
# See docs/pilot/CERTIFICATE-AND-CREDENTIAL-PROVISIONING.md

# 6. Start the daemon
/opt/matching-engine/bin/matching-engine-daemon /opt/matching-engine/config/platform.conf
```
