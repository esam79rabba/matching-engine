# Certificate Steps — pilot.yourdomain.com

---

## Option A: Let's Encrypt (DNS Ready)

**Prerequisites**: `pilot.yourdomain.com` resolves to the pilot host, port 80 open.

```bash
# On the pilot host:

# 1. Install certbot
sudo apt-get update && sudo apt-get install -y certbot

# 2. Stop anything on port 80 (certbot needs it for validation)
docker-compose down 2>/dev/null

# 3. Obtain certificate
sudo certbot certonly --standalone -d pilot.yourdomain.com

# 4. Copy to project
sudo cp /etc/letsencrypt/live/pilot.yourdomain.com/fullchain.pem deploy/pilot-env/tls/server.crt
sudo cp /etc/letsencrypt/live/pilot.yourdomain.com/privkey.pem deploy/pilot-env/tls/server.key
sudo chmod 644 deploy/pilot-env/tls/server.crt
sudo chmod 600 deploy/pilot-env/tls/server.key

# 5. Verify
openssl x509 -in deploy/pilot-env/tls/server.crt -noout -subject -dates
# Expected: subject=CN=pilot.yourdomain.com, valid 90 days

# 6. Restart container
docker-compose up -d
```

## Option B: Self-Signed (DNS Not Ready Yet)

Use for dry-run only. Replace before participant connects.

```bash
# Generate self-signed for pilot.yourdomain.com
openssl req -new -x509 \
    -newkey rsa:2048 -nodes \
    -keyout deploy/pilot-env/tls/server.key \
    -out deploy/pilot-env/tls/server.crt \
    -days 30 \
    -subj "/CN=pilot.yourdomain.com/O=VenueOps"

chmod 600 deploy/pilot-env/tls/server.key

# Verify
openssl x509 -in deploy/pilot-env/tls/server.crt -noout -subject -dates
```

**Mark this as temporary. Replace with Let's Encrypt before March 31.**

## Swap from Self-Signed to CA-Signed

```bash
# 1. Obtain Let's Encrypt cert (Option A steps 1-4)
# 2. Restart container
docker-compose restart matching-engine
# 3. Verify
echo | openssl s_client -connect pilot.yourdomain.com:19876 2>/dev/null | head -5
# Should show: depth=2 ... Let's Encrypt
```

## Certificate Checklist

| # | Check | Done |
|:-:|-------|:----:|
| 1 | CN matches `pilot.yourdomain.com` | |
| 2 | Not expired (check `Not After` date) | |
| 3 | Private key permissions 600 | |
| 4 | TLS handshake succeeds from external | |
| 5 | Participant's FIX client trusts the cert chain | |
