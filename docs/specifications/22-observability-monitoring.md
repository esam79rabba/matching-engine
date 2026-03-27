# 22. Observability, Monitoring, and Support Model

## 22.1 Observability Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                    PLATFORM COMPONENTS                         │
│  Engine │ Gateways │ Risk │ MD │ PostTrade │ Ops │ RefData   │
│    │         │        │     │      │         │       │        │
│    └─────────┴────────┴─────┴──────┴─────────┴───────┘        │
│              │ structured logs, metrics, events                │
└──────────────┼────────────────────────────────────────────────┘
               │
    ┌──────────▼──────────┐
    │  COLLECTION LAYER    │
    │  ┌────────────────┐  │
    │  │ Metrics Agent   │  │  (Prometheus/OpenTelemetry collector)
    │  │ Log Shipper     │  │  (Fluentd/Vector)
    │  │ Trace Collector │  │  (Jaeger/Zipkin — non-hot-path only)
    │  └────────────────┘  │
    └──────────┬──────────┘
               │
    ┌──────────▼──────────┐
    │  STORAGE LAYER       │
    │  ┌────────────────┐  │
    │  │ Metrics DB      │  │  (Prometheus TSDB / VictoriaMetrics)
    │  │ Log Store       │  │  (Elasticsearch / Loki)
    │  │ Trace Store     │  │  (Jaeger / Tempo)
    │  └────────────────┘  │
    └──────────┬──────────┘
               │
    ┌──────────▼──────────┐
    │  PRESENTATION LAYER  │
    │  ┌────────────────┐  │
    │  │ Dashboards      │  │  (Grafana)
    │  │ Alert Manager   │  │  (Alertmanager / PagerDuty)
    │  │ SLA Reports     │  │  (Custom reporting)
    │  └────────────────┘  │
    └─────────────────────┘
```

## 22.2 Metrics

### 22.2.1 Business Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `orders_total` | Counter | partition, instrument, side, type | Total orders received |
| `orders_rejected_total` | Counter | partition, reason | Rejected orders |
| `executions_total` | Counter | partition, instrument | Executed trades |
| `volume_total` | Counter | instrument | Traded volume |
| `turnover_total` | Counter | instrument, currency | Traded notional value |
| `active_orders` | Gauge | partition, instrument | Current resting orders |
| `active_sessions` | Gauge | gateway_type | Connected participant sessions |
| `participants_active` | Gauge | | Participants with at least one order |
| `auction_equilibrium_price` | Gauge | instrument | Latest auction equilibrium price |
| `circuit_breaker_activations` | Counter | instrument | Circuit breaker trigger count |
| `kill_switch_activations` | Counter | scope | Kill switch activation count |

### 22.2.2 Performance Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `matching_latency_ns` | Histogram | partition | Matching engine processing latency |
| `gateway_roundtrip_ns` | Histogram | gateway, type | Gateway wire-to-wire latency |
| `journal_write_ns` | Histogram | partition | Journal write latency |
| `risk_check_ns` | Histogram | | Pre-trade risk check latency |
| `market_data_publish_ns` | Histogram | | Engine event to multicast latency |
| `sequencer_latency_ns` | Histogram | partition | Sequencer processing latency |
| `queue_depth` | Gauge | queue_name | Inter-thread queue depth |
| `throughput_msg_per_sec` | Gauge | component | Current message processing rate |

### 22.2.3 Infrastructure Metrics

| Metric | Type | Labels | Description |
|--------|------|--------|-------------|
| `cpu_utilization_pct` | Gauge | host, core | CPU utilization per core |
| `memory_used_bytes` | Gauge | host, process | Memory usage |
| `context_switches` | Counter | host, core | Context switches per core |
| `nic_rx_packets` | Counter | host, interface | NIC received packets |
| `nic_tx_packets` | Counter | host, interface | NIC transmitted packets |
| `nic_drops` | Counter | host, interface | NIC dropped packets |
| `ptp_offset_ns` | Gauge | host | PTP clock offset from grandmaster |
| `disk_write_iops` | Gauge | host, device | Disk write IOPS |
| `disk_write_latency_ns` | Histogram | host, device | Disk write latency |

## 22.3 Structured Logging

### 22.3.1 Log Format
```json
{
  "timestamp": "2026-03-24T09:15:23.456789012Z",
  "level": "INFO",
  "component": "matching-engine",
  "partition": 1,
  "event": "order_matched",
  "order_id": 1099511627776,
  "instrument_id": 12345,
  "price": 10005,
  "quantity": 100,
  "latency_ns": 3250,
  "correlation_id": "a1b2c3d4"
}
```

### 22.3.2 Log Levels
- **ERROR**: System malfunction requiring immediate attention
- **WARN**: Degraded performance or unusual condition
- **INFO**: Significant business events (session start, instrument state change)
- **DEBUG**: Detailed processing information (not enabled in production hot path)

### 22.3.3 Hot Path Logging
- **No logging in matching engine hot path** — journal IS the log
- Gateway, risk, and market data may log at INFO level for significant events
- Debug logging enabled only in non-production environments
- Log writes must NEVER block the hot path (async only)

## 22.4 Alerting

### 22.4.1 Alert Severity Levels

| Severity | Response Time | Escalation | Examples |
|----------|-------------|------------|---------|
| **P1 — Critical** | < 5 minutes | Immediate; CTO notified | Engine down, data loss, security breach |
| **P2 — High** | < 15 minutes | Operations lead | Gateway failover, latency spike > 5× baseline |
| **P3 — Medium** | < 1 hour | On-call engineer | Elevated error rate, approaching capacity |
| **P4 — Low** | Next business day | Team backlog | Non-critical warnings, performance degradation |

### 22.4.2 Critical Alert Rules

| Alert | Condition | Action |
|-------|-----------|--------|
| Engine heartbeat lost | No heartbeat > 2 seconds | Auto-restart; page on-call |
| Matching latency spike | p99 > 5× baseline for > 10 seconds | Investigate; page if sustained |
| Journal write failure | Any journal write error | Halt trading; page critical |
| Gateway all sessions lost | All sessions on a gateway disconnect | Investigate; failover if needed |
| PTP clock drift | Offset > 1 µs | Alert; investigate sync |
| Disk space < 10% | Journal storage running low | Alert; extend or rotate |
| Kill switch activated | Any kill switch event | Alert; investigate cause |
| Circuit breaker cascade | > 5 instruments in volatility auction simultaneously | Alert operations; investigate |
| Packet drops | Any NIC drops | Alert; investigate network |
| Memory > 90% | Process memory usage | Alert; investigate growth |

## 22.5 Support Model

### 22.5.1 Support Tiers

| Tier | Role | Responsibility | Hours |
|------|------|---------------|-------|
| **L1** | Operations / NOC | Alert triage, runbook execution, participant support | Trading hours + 1 hour |
| **L2** | Engineering on-call | Incident investigation, root cause analysis, fix | 24/7 on-call (pager) |
| **L3** | Core engineering | Complex bugs, architecture issues, performance | Business hours + escalation |
| **Participant support** | Connectivity team | Participant onboarding, certification, connectivity issues | Business hours |

### 22.5.2 SLA Targets

| Service | Metric | Target |
|---------|--------|--------|
| Trading system uptime | Availability during trading hours | 99.99% |
| Incident response (P1) | Time to acknowledge | < 5 minutes |
| Incident resolution (P1) | Time to restore trading | < 30 minutes |
| Incident resolution (P2) | Time to restore normal | < 2 hours |
| Participant connectivity issue | Response time | < 15 minutes during trading |
| Market data gap recovery | Time to provide snapshot | < 10 seconds |
| Post-incident report | Root cause analysis delivery | < 24 hours for P1/P2 |

## 22.6 Synthetic Monitoring

| Probe | Frequency | Purpose |
|-------|-----------|---------|
| Gateway logon/logout | Every 60 seconds | Verify gateway accepts sessions |
| Order submit/cancel | Every 60 seconds | Verify end-to-end order lifecycle |
| Market data subscribe | Every 30 seconds | Verify market data feed active |
| Snapshot request | Every 5 minutes | Verify snapshot recovery works |
| Admin API health | Every 30 seconds | Verify operations console accessible |
| Risk service health | Every 30 seconds | Verify risk checks functional |
| PTP sync check | Every 10 seconds | Verify clock synchronization |
| Journal write test | Every 10 seconds | Verify journal writable |
