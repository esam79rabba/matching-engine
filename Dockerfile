# ============================================================
# Matching Engine — Multi-Stage Docker Build
# ============================================================
# Stage 1: Build (full toolchain)
# Stage 2: Runtime (minimal image with binary + deps only)
# ============================================================

# --- Build Stage ---
FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN mkdir -p build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
    && make -j$(nproc)

# --- Runtime Stage ---
FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --no-create-home matching-engine

WORKDIR /opt/matching-engine

# Copy binary
COPY --from=builder /src/build/daemon/matching-engine-daemon bin/

# Copy default config (can be overridden by volume mount)
COPY deploy/pilot-env/platform.conf config/platform.conf
COPY deploy/pilot-env/instruments.json config/instruments.json
COPY deploy/pilot-env/sessions.json config/sessions.json

# Create data directories
RUN mkdir -p data/journal data/snapshots data/eod data/fix-sessions data/msg-store logs \
    && chown -R matching-engine:matching-engine /opt/matching-engine

# Ports: FIX, Market Data, Ops HTTP
EXPOSE 9876 9877 8080

USER matching-engine

ENTRYPOINT ["bin/matching-engine-daemon"]
CMD ["config/platform.conf"]
