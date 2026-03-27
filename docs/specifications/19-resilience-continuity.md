# 19. Operational Resilience and Business Continuity

## 19.1 Resilience Tiers

| Component | Tier | Availability Target | RTO | RPO | Redundancy Model |
|-----------|------|-------------------|-----|-----|-------------------|
| Matching Engine | Tier 1 | 99.99% | < 60s (warm), < 5s (hot standby) | Zero | Active + hot standby |
| Sequencer | Tier 1 | 99.99% | < 5s | Zero | Active-passive pair |
| Journal | Tier 1 | 99.99% | < 60s | Zero | Synchronous replication |
| FIX/Binary Gateways | Tier 1 | 99.99% | < 30s | Zero | Active-passive pairs |
| Market Data Publisher | Tier 1 | 99.99% | < 30s | Zero | Active-passive |
| Pre-Trade Risk | Tier 1 | 99.99% | < 30s | Near-zero | Active-passive |
| Reference Data Service | Tier 2 | 99.9% | < 5 min | Zero | Primary + read replicas |
| Post-Trade Service | Tier 2 | 99.9% | < 5 min | Zero | Active-passive |
| Operations Console | Tier 2 | 99.9% | < 10 min | Zero | Load balanced |
| Surveillance Export | Tier 2 | 99.9% | < 15 min | Near-zero | Replay from journal |
| Participant Admin | Tier 3 | 99.5% | < 30 min | Zero | Standard HA |
| Monitoring Stack | Tier 2 | 99.9% | < 10 min | Minutes | Standard HA |

## 19.2 Failure Scenarios and Recovery

### 19.2.1 Matching Engine Process Crash

| Aspect | Detail |
|--------|--------|
| **Detection** | Heartbeat failure detected by watchdog within 1 second |
| **Impact** | Orders for affected partition(s) not processed; market data stops |
| **Recovery** | Warm restart: load latest snapshot + replay journal from snapshot point |
| **RTO** | < 60 seconds for 100K resting orders |
| **RPO** | Zero — all state in journal |
| **Participant Impact** | Orders queued during restart; no order loss; session maintained at gateway |
| **Automation** | Automated restart by watchdog; alert to operations |

### 19.2.2 Gateway Failure

| Aspect | Detail |
|--------|--------|
| **Detection** | Health check failure; client heartbeat timeout |
| **Impact** | Connected participants lose session; cancel-on-disconnect may trigger |
| **Recovery** | Standby gateway promotes; participants reconnect; FIX sequence gap recovery |
| **RTO** | < 30 seconds |
| **Participant Impact** | Brief disconnection; CoD orders cancelled per policy; reconnect and resume |

### 19.2.3 Sequencer Failure

| Aspect | Detail |
|--------|--------|
| **Detection** | Heartbeat failure from matching engine or gateways |
| **Impact** | No new messages sequenced; effective trading halt |
| **Recovery** | Standby sequencer promotes with journal replay to sync state |
| **RTO** | < 5 seconds (hot standby) |
| **RPO** | Zero or near-zero (last committed journal entry) |

### 19.2.4 Full Host Failure

| Aspect | Detail |
|--------|--------|
| **Detection** | Network/heartbeat failure; BMC/IPMI alerts |
| **Impact** | All components on host unavailable |
| **Recovery** | Failover to standby host with journal-replayed state |
| **RTO** | < 5 minutes (full host failover with state rebuild) |
| **RPO** | Zero (journal replicated to standby) |

### 19.2.5 Data Center Failure (DR Scenario)

| Aspect | Detail |
|--------|--------|
| **Detection** | Loss of connectivity to primary site |
| **Impact** | Complete primary site unavailable |
| **Recovery** | DR site activation; journal replayed from replication lag point |
| **RTO** | < 5 minutes |
| **RPO** | Near-zero (replication lag, typically < 1 second) |
| **Participant Impact** | Participants reconnect to DR gateway IPs; brief trading interruption |

### 19.2.6 Network Partition Between Components

| Aspect | Detail |
|--------|--------|
| **Detection** | Heartbeat failures; message delivery timeouts |
| **Impact** | Depends on partition location; may cause gateway isolation |
| **Recovery** | Reconnection and state sync; journal replay for any missed events |
| **Prevention** | Dual network paths; bonded interfaces; separate control plane network |

## 19.3 DR Architecture

### 19.3.1 Active-Passive Design

```
PRIMARY SITE                           DR SITE
┌──────────────┐                      ┌──────────────┐
│  Gateways    │                      │  Gateways    │ (standby)
│  Sequencer   │─── Journal Sync ────→│  Sequencer   │ (replaying)
│  Engine      │                      │  Engine      │ (warm standby)
│  Market Data │                      │  Market Data │ (standby)
│  Risk        │                      │  Risk        │ (standby)
│  Journal     │─── Sync Repl ──────→│  Journal     │ (replica)
│  Snapshots   │─── Async Copy ─────→│  Snapshots   │ (copy)
└──────────────┘                      └──────────────┘
```

### 19.3.2 Failover Procedure
1. Primary site failure detected (automated or manual declaration)
2. DR site promotes: sequencer takes over sequencing; engine loads latest snapshot + replays journal
3. Gateways activate on DR site; DNS/anycast updated to point to DR
4. Participants reconnect (or automatic reconnect if using anycast/VIP)
5. Trading resumes on DR site
6. Post-failover: reconcile any in-flight messages; verify journal continuity

### 19.3.3 Failback Procedure
1. Primary site restored and verified
2. Primary replays journal from DR to sync state
3. Planned failback during non-trading hours
4. Participants notified in advance
5. Brief controlled outage for switchover
6. Verify primary fully operational; resume normal operations

## 19.4 Business Continuity

### 19.4.1 BCP Requirements

| Requirement | Specification |
|-------------|--------------|
| BCP documentation | Written, tested, and auditable business continuity plan |
| DR drills | Minimum quarterly; full failover drill minimum annually |
| Communication plan | Participant and regulator notification procedures |
| Escalation matrix | Defined escalation paths with contact details |
| Recovery playbooks | Step-by-step runbooks for each failure scenario |
| Staff redundancy | Key roles have trained backups; no single point of human failure |
| Remote operations | Capability to operate the exchange remotely in emergency |

### 19.4.2 Resilience Testing

| Test Type | Frequency | Scope |
|-----------|-----------|-------|
| Component failover | Monthly | Individual component failure and recovery |
| Gateway failover | Monthly | Active-passive gateway switchover |
| Engine warm restart | Monthly | Snapshot load + journal replay under load |
| Full DR failover | Quarterly | Complete site failover and trading resumption |
| Network failure simulation | Quarterly | Network partition, latency injection |
| Chaos testing | Monthly (non-hot-path) | Random failure injection in platform services |
| Load + failure combined | Quarterly | Failure injection under peak load conditions |
| Participant reconnect drill | Semi-annually | Coordinated participant reconnection exercise |

## 19.5 Maintenance Model

### 19.5.1 Rolling Upgrade (Non-Hot-Path)
- Platform services (gateways, risk, reference data, operations) upgradeable without trading interruption
- Blue-green deployment for stateless services
- Canary deployment for gradual rollout

### 19.5.2 Matching Engine Upgrade
- Requires brief trading halt (< 5 minutes with warm restart)
- Performed during scheduled maintenance window (outside trading hours)
- Rollback: restart previous version from same journal point
- Zero-downtime upgrade is a Phase 4 goal (not Phase 1)

### 19.5.3 Maintenance Windows
- Scheduled: Outside trading hours, minimum 2 weeks notice to participants
- Emergency: As needed with immediate notification
- Maximum planned downtime: 4 hours/month
