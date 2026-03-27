# Monitoring and Alerting Rules — FR-1 Draft

**Status**: Starter artifact — decision-independent
**Source**: §22 Observability, §15.10 (frozen baseline)
**Stack assumption**: Prometheus + Grafana (provisional recommendation)

---

## A. Service Health Alerts

| Alert | Metric / Signal | Condition | Severity | Owner | FR-1? |
|-------|----------------|-----------|:--------:|-------|:-----:|
| SVC-001 Service down | `up{job="..."}` | == 0 for > 30s | P1 | SRE | Yes |
| SVC-002 High error rate | `error_rate{service="..."}` | > 1% over 1 min | P2 | SRE | Yes |
| SVC-003 High response time | `http_request_duration_p99` | > 5s (admin API) | P3 | SRE | Yes |
| SVC-004 Process restart | `process_start_time` changes | Any restart | P2 | SRE | Yes |

## B. Gateway / Session Alerts

| Alert | Metric / Signal | Condition | Severity | Owner | FR-1? |
|-------|----------------|-----------|:--------:|-------|:-----:|
| GW-001 All sessions lost | `active_sessions{gw="..."}` | == 0 (was > 0) | P1 | Connectivity | Yes |
| GW-002 High reject rate | `order_rejects_total / orders_total` | > 5% over 5 min | P2 | Connectivity | Yes |
| GW-003 Session logon failure | `logon_failures_total` | > 3 in 1 min | P2 | Security | Yes |
| GW-004 Throttle activations | `throttle_rejects_total` | > 100 in 1 min | P3 | Connectivity | Yes |
| GW-005 CoD triggered | `cod_activations_total` | Any | P3 | Ops | Yes |

## C. Sequencer / Engine Alerts

| Alert | Metric / Signal | Condition | Severity | Owner | FR-1? |
|-------|----------------|-----------|:--------:|-------|:-----:|
| ENG-001 Engine heartbeat lost | Watchdog heartbeat miss | > 2 seconds | P1 | SRE | Yes |
| ENG-002 Matching latency spike | `matching_latency_p99` | > 2x baseline for > 10s | P2 | Engine | Yes |
| ENG-003 Queue depth high | `input_queue_depth{partition}` | > 80% capacity | P2 | Engine | Yes |
| ENG-004 Partition throughput drop | `orders_processed_per_sec` | < 50% of baseline | P2 | Engine | Yes |
| ENG-005 Context switches on pinned core | `context_switches{core}` | > 10/sec | P3 | SRE | Yes |

## D. Journal / Snapshot / Replay Alerts

| Alert | Metric / Signal | Condition | Severity | Owner | FR-1? |
|-------|----------------|-----------|:--------:|-------|:-----:|
| JNL-001 Journal write failure | `journal_write_errors_total` | > 0 | P1 | SRE | Yes |
| JNL-002 Journal write latency | `journal_write_latency_p99` | > 2x baseline | P2 | Engine | Yes |
| JNL-003 Disk space low | `journal_disk_free_pct` | < 10% | P1 | SRE | Yes |
| JNL-004 Snapshot failure | `snapshot_failures_total` | > 0 | P2 | SRE | Yes |
| JNL-005 Snapshot age | `time_since_last_snapshot` | > 5 min (configurable) | P3 | SRE | Yes |

## E. Market Data Alerts

| Alert | Metric / Signal | Condition | Severity | Owner | FR-1? |
|-------|----------------|-----------|:--------:|-------|:-----:|
| MD-001 Publication stopped | `md_updates_per_sec` | == 0 (trading active) | P1 | MD team | Yes |
| MD-002 Publication latency | `md_publish_latency_p99` | > 2x baseline | P2 | MD team | Yes |
| MD-003 Snapshot requests spike | `snapshot_requests_per_sec` | > 50/sec | P3 | MD team | Yes |

## F. Risk Control Alerts

| Alert | Metric / Signal | Condition | Severity | Owner | FR-1? |
|-------|----------------|-----------|:--------:|-------|:-----:|
| RSK-001 Kill switch activated | `kill_switch_activations` | Any | P1 | Ops + Risk | Yes |
| RSK-002 Fat-finger reject spike | `fat_finger_rejects_total` | > 50 in 1 min per participant | P3 | Risk | Yes |
| RSK-003 Risk check latency | `risk_check_latency_p99` | > 2x baseline | P2 | Risk | Yes |

## G. Security / Auth Alerts

| Alert | Metric / Signal | Condition | Severity | Owner | FR-1? |
|-------|----------------|-----------|:--------:|-------|:-----:|
| SEC-001 Auth failure burst | `auth_failures_total` | > 10 in 1 min from same IP | P2 | Security | Yes |
| SEC-002 Certificate expiry | `cert_expiry_days` | < 30 days | P3 | Security | Yes |
| SEC-003 Unauthorized API access | `http_403_total` | > 20 in 5 min | P2 | Security | Yes |

## H. Infrastructure / PTP / Clock Alerts

| Alert | Metric / Signal | Condition | Severity | Owner | FR-1? |
|-------|----------------|-----------|:--------:|-------|:-----:|
| INF-001 PTP clock drift | `ptp_offset_ns` | > 1000 ns (1 µs) | P2 | SRE | Yes |
| INF-002 PTP grandmaster lost | `ptp_gm_status` | != LOCKED | P1 | SRE | Yes |
| INF-003 NIC packet drops | `nic_rx_drops_total` | > 0 | P2 | SRE | Yes |
| INF-004 CPU utilization | `cpu_util{core=pinned}` | > 95% sustained 1 min | P2 | SRE | Yes |
| INF-005 Memory usage | `process_memory_bytes` | > 90% allocated | P2 | SRE | Yes |

## I. Business / Venue-State Alerts

| Alert | Metric / Signal | Condition | Severity | Owner | FR-1? |
|-------|----------------|-----------|:--------:|-------|:-----:|
| BIZ-001 Circuit breaker cascade | `circuit_breaker_active_count` | > 5 instruments simultaneously | P2 | Ops | Phase 2 |
| BIZ-002 Session start delayed | `session_start_delay_sec` | > 60 sec past scheduled time | P2 | Ops | Yes |
| BIZ-003 No trades for > N min | `time_since_last_trade` | > 15 min (during continuous) | P3 | Ops | Yes |
| BIZ-004 Participant zero activity | `participant_orders_1h` | == 0 for active MM | P3 | Ops | Phase 2 |

---

## Dashboard Groups

| Dashboard | Content | Primary Audience |
|-----------|---------|:----------------:|
| **System Health** | All SVC, INF alerts; service map; uptime | SRE / NOC |
| **Engine Performance** | ENG latency histograms; throughput; queue depths | Engineering |
| **Gateway & Sessions** | GW active sessions; logon/logout; reject rates; throttle | Connectivity |
| **Journal & Recovery** | JNL write latency; disk usage; snapshot age; replay status | SRE |
| **Market Data** | MD publication rate; latency; snapshot requests | MD team |
| **Risk & Controls** | RSK reject rates; kill switch history; limit utilization | Risk / Ops |
| **Security** | SEC auth failures; cert status; access anomalies | Security |
| **Market Operations** | BIZ session status; instrument states; trade activity; circuit breakers | Market Ops |
| **Business KPIs** | Orders/sec; trades/sec; volume; turnover; participant count | Management |

## Top 10 NOC / Ops KPIs

| # | KPI | Source |
|---|-----|--------|
| 1 | System availability (trading hours) | SVC-001 aggregate |
| 2 | Active participant sessions | GW active_sessions |
| 3 | Orders per second (aggregate) | Engine throughput |
| 4 | Trades per second | Execution events |
| 5 | Matching latency p99 | ENG-002 |
| 6 | Gateway round-trip p99 | GW latency histogram |
| 7 | Journal disk free % | JNL-003 |
| 8 | PTP clock offset | INF-001 |
| 9 | Kill switch activations (today) | RSK-001 |
| 10 | Open P1/P2 alerts | Alert manager |

## Top 10 Engineering / Performance KPIs

| # | KPI | Source |
|---|-----|--------|
| 1 | Matching latency p50 / p99 / p99.9 | Per-partition histogram |
| 2 | Gateway latency p50 / p99 | Per-gateway histogram |
| 3 | Journal write latency p99 | Per-partition |
| 4 | Input queue depth (max) | Per-partition gauge |
| 5 | Context switches on pinned cores | Per-core counter |
| 6 | L3 cache miss rate | perf counters |
| 7 | Throughput (msg/sec per partition) | Counter rate |
| 8 | Memory allocation rate (hot path) | Should be 0 after warmup |
| 9 | NIC packet drops | Counter |
| 10 | Build + test pipeline duration | CI metrics |
