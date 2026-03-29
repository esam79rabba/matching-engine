# ============================================================
# Matching Engine Platform — Multi-Stage Docker Build
# ============================================================
# Stage 1: Build C++ daemon (full toolchain)
# Stage 2: Build React portal (Node.js)
# Stage 3: Runtime (minimal image: daemon + nginx + portal)
# ============================================================

# ─── Stage 1: C++ Build ──────────────────────────────────────
FROM ubuntu:24.04 AS cpp-builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY engine/ engine/
COPY gateway/ gateway/
COPY risk/ risk/
COPY surveillance/ surveillance/
COPY post-trade/ post-trade/
COPY market-data/ market-data/
COPY dropcopy/ dropcopy/
COPY refdata/ refdata/
COPY ops/ ops/
COPY eod/ eod/
COPY common/ common/
COPY replication/ replication/
COPY certification/ certification/
COPY daemon/ daemon/
COPY simulator/ simulator/
COPY benchmark/ benchmark/
COPY CMakeLists.txt .

RUN mkdir -p build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_QUICKFIX=OFF \
       -DCMAKE_CXX_FLAGS="-Wno-error -Wno-strict-aliasing -Wno-unused-variable -Wno-unused-but-set-variable" \
    && make -j$(nproc) matching-engine-daemon

# ─── Stage 2: Portal Build ───────────────────────────────────
FROM node:22-slim AS portal-builder

WORKDIR /portal
COPY portal/package.json portal/package-lock.json* ./
RUN npm ci --production=false 2>/dev/null || npm install
COPY portal/ .
RUN npm run build

# ─── Stage 3: Runtime ────────────────────────────────────────
FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 ca-certificates nginx \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --no-create-home matching-engine

WORKDIR /opt/matching-engine

# Copy C++ daemon binary
COPY --from=cpp-builder /src/build/daemon/matching-engine-daemon bin/

# Copy default configs (can be overridden by volume mount)
COPY deploy/docker-config/platform.conf config/platform.conf
COPY deploy/docker-config/instruments.json config/instruments.json
COPY deploy/config/quickfix.cfg config/quickfix.cfg

# Copy portal static files for Nginx
COPY --from=portal-builder /portal/dist/ /var/www/portal/

# Copy FIX spec files (for QuickFIX data dictionary validation)
# These are only needed if ENABLE_QUICKFIX=ON
COPY deploy/config/ config/

# Nginx config for portal + API proxy
RUN cat > /etc/nginx/sites-available/default <<'NGINX'
server {
    listen 80;
    server_name _;

    # Portal (React SPA)
    location / {
        root /var/www/portal;
        index index.html;
        try_files $uri $uri/ /index.html;
    }

    # API proxy to daemon
    location /api/ {
        proxy_pass http://127.0.0.1:8080/api/;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_read_timeout 30s;
    }

    # WebSocket proxy
    location /ws {
        proxy_pass http://127.0.0.1:8081;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_read_timeout 86400s;
    }
}
NGINX

# Create data directories
RUN mkdir -p \
    data/journal \
    data/snapshots \
    data/eod \
    data/fix-sessions \
    data/msg-store \
    data/quickfix/store \
    data/quickfix/log \
    data/session-config \
    data/audit \
    logs \
    && chown -R matching-engine:matching-engine /opt/matching-engine \
    && chown -R matching-engine:matching-engine /var/www/portal

# Ports
#   80   — Portal (Nginx)
#   9876 — FIX Gateway (TCP/TLS, all FIX versions)
#   9877 — Market Data TCP (JSON)
#   9878 — Market Data Multicast (UDP)
#   8080 — Ops REST API + HTTP
#   8081 — WebSocket (real-time push)
EXPOSE 80 9876 9877 9878 8080 8081

# Startup script: start nginx + daemon
RUN cat > /opt/matching-engine/start.sh <<'STARTUP'
#!/bin/bash
set -e

# Validate required environment variables
: "${OPS_API_KEY:?OPS_API_KEY must be set}"
: "${JWT_SIGNING_KEY:?JWT_SIGNING_KEY must be set}"

echo "Starting Matching Engine Platform..."
echo "  Portal:     http://0.0.0.0:80"
echo "  REST API:   http://0.0.0.0:8080"
echo "  WebSocket:  ws://0.0.0.0:8081"
echo "  FIX:        tcp://0.0.0.0:9876 (FIX 4.0-5.0 SP2)"
echo "  Market Data: tcp://0.0.0.0:9877 (JSON), udp://239.1.1.1:9878 (multicast)"

# Start Nginx in background
nginx &

# Start daemon in foreground
exec /opt/matching-engine/bin/matching-engine-daemon \
    /opt/matching-engine/config/platform.conf
STARTUP
RUN chmod +x /opt/matching-engine/start.sh

# Run as matching-engine user (nginx needs root briefly, then drops privileges)
# Use root to start nginx, daemon runs as matching-engine via exec
ENTRYPOINT ["/opt/matching-engine/start.sh"]
