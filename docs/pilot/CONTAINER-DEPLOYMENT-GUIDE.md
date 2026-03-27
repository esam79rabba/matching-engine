# Container Deployment Guide

## Quick Start

```bash
docker build -t matching-engine .
docker-compose up -d
```

## Prerequisites

- Docker 24+ and Docker Compose v2
- TLS certificates in `deploy/pilot-env/tls/`
- Config files in `deploy/docker-config/`

## Directory Layout

```
deploy/docker-config/
  platform.conf       # Container config (paths are container-internal)
  instruments.json    # Instrument definitions
  sessions.json       # Session/participant config
deploy/pilot-env/tls/
  server.crt          # TLS certificate
  server.key          # TLS private key
```

## Building

```bash
docker build -t matching-engine .
```

## Running

```bash
docker-compose up -d
```

## Ports

| Port | Protocol     | Description          |
|------|-------------|----------------------|
| 9876 | FIX (TLS)   | FIX order gateway    |
| 9877 | TCP/JSON    | Market data feed     |
| 8080 | HTTP        | Ops console          |

## Data Persistence

Docker volumes preserve state across restarts:

| Volume          | Contents                                     |
|-----------------|----------------------------------------------|
| journal-data    | Journal segments                             |
| fix-sessions    | FIX session state (survives restart)         |
| msg-store       | Persistent FIX message store                 |
| eod-data        | EOD output files                             |
| snapshot-data   | Partition snapshots                          |

## Restart Behavior

```bash
docker-compose restart matching-engine
```

On restart:

- Journal persists (volume)
- FIX session store persists (volume)
- Message store persists (volume)
- Participants can reconnect with resumed sequence numbers

When a participant reconnects after a restart:

1. The participant sends a Logon with their current MsgSeqNum
2. The acceptor restores persisted inbound/outbound sequence numbers
3. If the participant's seq is ahead of the persisted value, the acceptor advances to match
4. The Logon ack carries the persisted outbound sequence number in tag 34
5. Normal message flow resumes without message loss

## Customization

Mount your own config by overriding the config volume:

```yaml
volumes:
  - /path/to/my-config:/opt/matching-engine/config:ro
```

## Health Check

```bash
curl http://localhost:8080/api/v1/health
```

## Logs

```bash
docker-compose logs -f matching-engine
```

## Stopping

```bash
# Stop and preserve all volumes (data retained)
docker-compose down

# Stop and DELETE all data (destructive!)
docker-compose down -v
```

## Troubleshooting

| Issue          | Resolution                                           |
|----------------|------------------------------------------------------|
| Port conflict  | Set `FIX_PORT`, `MD_PORT`, `OPS_PORT` env vars       |
| TLS error      | Verify certs are mounted at `config/tls/`            |
| Config error   | Check `docker-compose logs matching-engine`          |
| Seq mismatch   | Check `fix-sessions` volume for persisted state      |
| Stale sessions | Delete `fix-sessions` volume for a clean start       |
