# 15. Detailed Non-Functional Requirements

## 15.1 Latency

> **Deployment Profile Context**: The following latency targets are stated for **Deployment Profile A** (bare-metal colocation, kernel bypass, CPU pinning, PTP, NIC timestamping). For Profile B, multiply targets by approximately 4×. For Profile C, multiply by approximately 20×. All targets are engineering goals validated through performance testing under sustained load, not contractual guarantees. See §21 for deployment profile definitions.

| REQ ID | Requirement | Target (Profile A) | Measurement Point | Classification |
|--------|------------|--------|-------------------|----------|
| NFR-LAT-001 | Core matching latency (p50) | < 1 µs | Sequencer output → engine output | Engineering target |
| NFR-LAT-002 | Core matching latency (p99) | < 5 µs | Sequencer output → engine output | Engineering target |
| NFR-LAT-003 | Core matching latency (p99.9) | < 10 µs | Sequencer output → engine output | Engineering target |
| NFR-LAT-004 | Binary gateway round-trip (p50) | < 10 µs | NIC receive → NIC send (ack) | Engineering target |
| NFR-LAT-005 | Binary gateway round-trip (p99) | < 25 µs | NIC receive → NIC send (ack) | Engineering target |
| NFR-LAT-006 | FIX gateway round-trip (p50) | < 50 µs | TCP receive → TCP send | Engineering target |
| NFR-LAT-007 | FIX gateway round-trip (p99) | < 100 µs | TCP receive → TCP send | Engineering target |
| NFR-LAT-008 | Market data publication (p50) | < 5 µs | Engine event → multicast send | Engineering target |
| NFR-LAT-009 | Market data publication (p99) | < 20 µs | Engine event → multicast send | Engineering target |
| NFR-LAT-010 | Pre-trade risk check | < 5 µs | Message receive → risk decision | Engineering target |
| NFR-LAT-011 | Drop copy delivery (p99) | < 100 µs | Engine event → drop copy send | Engineering target |
| NFR-LAT-012 | WebSocket/REST round-trip (p99) | < 10 ms | HTTP receive → HTTP response | Engineering target |

> **Classification note**: "Engineering target" means the value is a design goal validated through performance testing for the stated deployment profile. It is mandatory to *measure and report* these metrics. Achieving the numeric target depends on the deployment profile and hardware selection. "Mandatory" is reserved for behavioral requirements (determinism, correctness, completeness) that must hold regardless of deployment profile.

**Design Notes**:
- Latency targets assume colocation deployment on bare-metal hardware
- Cloud deployment will degrade latency by 10–100x depending on configuration
- All latency measurements must include hardware NIC timestamps for accuracy
- Latency must be measured under load (sustained throughput), not idle conditions

## 15.2 Jitter

| REQ ID | Requirement | Target (Profile A) | Classification |
|--------|------------|--------|----------|
| NFR-JIT-001 | Matching engine jitter (p99 - p50) | < 5 µs | Engineering target |
| NFR-JIT-002 | Gateway jitter (p99 - p50) | < 20 µs | Engineering target |
| NFR-JIT-003 | Market data jitter (p99 - p50) | < 15 µs | Engineering target |

**Design Notes**:
- Low jitter is as important as low latency for fairness
- Sources of jitter: GC pauses (JVM), context switches, cache misses, memory allocation, I/O waits
- Mitigation: CPU pinning, memory pre-allocation, no GC on hot path (or GC-free design), kernel bypass

## 15.3 Throughput

> Throughput targets assume Deployment Profile A hardware (bare-metal, CPU pinning, NVMe). Profile B/C targets scale with hardware capability. It is mandatory to *measure and report* throughput; numeric targets are engineering goals validated through testing.

| REQ ID | Requirement | Target (Profile A) | Classification |
|--------|------------|--------|----------|
| NFR-THR-001 | Sustained order rate per partition | ≥ 1,000,000 msg/s | Engineering target |
| NFR-THR-002 | Burst order rate per partition | ≥ 5,000,000 msg/s (10 second burst) | Stretch target |
| NFR-THR-003 | Sustained order rate aggregate | ≥ 10,000,000 msg/s (across all partitions) | Stretch target |
| NFR-THR-004 | Market data output rate | ≥ 5,000,000 updates/s aggregate | Engineering target |
| NFR-THR-005 | FIX gateway aggregate throughput | ≥ 500,000 msg/s per gateway instance | Engineering target |
| NFR-THR-006 | Binary gateway aggregate throughput | ≥ 2,000,000 msg/s per gateway instance | Engineering target |
| NFR-THR-007 | Mass quote throughput | ≥ 100,000 mass quotes/s (each updating up to 100 instruments) | Engineering target |
| NFR-THR-008 | Journal write throughput | ≥ 2,000,000 entries/s per partition | Engineering target |

## 15.4 Scalability

| REQ ID | Requirement | Target | Priority |
|--------|------------|--------|----------|
| NFR-SCL-001 | Max instruments per partition | ≥ 1,000 active instruments | Mandatory |
| NFR-SCL-002 | Max partitions per host | ≥ 8 (depends on core count) | Mandatory |
| NFR-SCL-003 | Max instruments per platform | ≥ 100,000 | Highly Recommended |
| NFR-SCL-004 | Max concurrent sessions (FIX) | ≥ 1,000 | Mandatory |
| NFR-SCL-005 | Max concurrent sessions (binary) | ≥ 500 | Mandatory |
| NFR-SCL-006 | Max resting orders per partition | ≥ 1,000,000 | Mandatory |
| NFR-SCL-007 | Max resting orders per platform | ≥ 10,000,000 | Highly Recommended |
| NFR-SCL-008 | Max participants | ≥ 500 | Mandatory |
| NFR-SCL-009 | Max price levels per instrument | ≥ 10,000 | Mandatory |
| NFR-SCL-010 | Horizontal scaling | Add partitions without downtime | Highly Recommended |

**Scaling Model**:
- **Vertical**: Increase CPU cores, memory per host → more partitions per host
- **Horizontal**: Add hosts → distribute partitions across hosts
- **Instrument re-partitioning**: Move instruments between partitions during maintenance windows

## 15.5 Availability

| REQ ID | Requirement | Target | Priority |
|--------|------------|--------|----------|
| NFR-AVL-001 | System availability during trading hours | ≥ 99.99% (< 52 minutes downtime/year) | Mandatory |
| NFR-AVL-002 | Planned maintenance window | Outside trading hours; < 4 hours/month | Mandatory |
| NFR-AVL-003 | Unplanned downtime per incident | < 5 minutes (warm restart) | Mandatory |
| NFR-AVL-004 | Gateway availability | ≥ 99.99% per gateway pair (active-passive) | Mandatory |
| NFR-AVL-005 | Market data availability | ≥ 99.99% during trading hours | Mandatory |
| NFR-AVL-006 | Operations console availability | ≥ 99.9% | Mandatory |

## 15.6 Recoverability

| REQ ID | Requirement | Target | Priority |
|--------|------------|--------|----------|
| NFR-REC-001 | Warm restart (same host) | < 60 seconds | Mandatory |
| NFR-REC-002 | Hot standby failover | < 5 seconds | Mandatory |
| NFR-REC-003 | DR site failover | < 5 minutes | Mandatory |
| NFR-REC-004 | Recovery Point Objective (RPO) | Zero data loss (journal replicated) | Mandatory |
| NFR-REC-005 | Recovery Time Objective (RTO) | < 5 minutes for full DR scenario | Mandatory |
| NFR-REC-006 | Point-in-time reconstruction | Any historical point from journal | Mandatory |
| NFR-REC-007 | Snapshot recovery | Load snapshot + replay in < 60 seconds for 100K orders | Mandatory |

## 15.7 Determinism

| REQ ID | Requirement | Target | Priority |
|--------|------------|--------|----------|
| NFR-DET-001 | Matching determinism | Identical input → bit-identical output, always | Mandatory |
| NFR-DET-002 | Replay fidelity | Journal replay produces identical state and events | Mandatory |
| NFR-DET-003 | No external non-determinism | No wall-clock, random, or external calls in matching path | Mandatory |
| NFR-DET-004 | Determinism verification | Automated tests verify replay determinism on every release | Mandatory |

## 15.8 Fairness

> **Fairness boundary**: Fairness guarantees apply at the **venue-controlled sequencing boundary** — the point where the sequencer assigns a monotonic sequence number to each inbound message. Network path variability outside the venue boundary (participant LAN, WAN, cross-connect) is outside the venue's fairness guarantee. The venue publishes per-gateway latency statistics so participants and regulators can verify fairness independently.

| REQ ID | Requirement | Target | Classification |
|--------|------------|--------|----------|
| NFR-FAR-001 | Gateway fairness at sequencing boundary | Max latency difference between gateways at sequencer: < 1 µs (Profile A with hardware sequencer), < 5 µs (Profile A/B with software time-based sequencer), < 50 µs (Profile C). | Engineering target (profile-dependent) |
| NFR-FAR-002 | No privileged paths | No hardware or software shortcut for any participant within the venue-controlled boundary | Mandatory |
| NFR-FAR-003 | Measurable fairness | Publish per-gateway latency statistics for regulatory review | Mandatory |
| NFR-FAR-004 | Batch fairness | Multi-gateway batching policy documented and transparent | Mandatory |
| NFR-FAR-005 | Fairness boundary transparency | Venue publishes documentation defining exactly where fairness guarantees begin and end | Mandatory |

## 15.9 Security

| REQ ID | Requirement | Target | Priority |
|--------|------------|--------|----------|
| NFR-SEC-001 | Authentication | All connections authenticated (mTLS, API key, or session credentials) | Mandatory |
| NFR-SEC-002 | Authorization | RBAC/ABAC on all APIs and operations | Mandatory |
| NFR-SEC-003 | Encryption in transit | TLS 1.3 for all non-colocation connections | Mandatory |
| NFR-SEC-004 | Encryption at rest | Journal and snapshot files encrypted at rest (AES-256) | Mandatory |
| NFR-SEC-005 | Secrets management | No hardcoded secrets; HSM for key storage | Mandatory |
| NFR-SEC-006 | Audit trail tamper evidence | Tamper-evident checksums on all audit records | Mandatory |
| NFR-SEC-007 | Penetration testing | Annual third-party penetration test | Mandatory |
| NFR-SEC-008 | Vulnerability scanning | Automated scanning in CI/CD pipeline | Mandatory |
| NFR-SEC-009 | Privileged access controls | Break-glass procedures for production access | Mandatory |
| NFR-SEC-010 | Network segmentation | Matching engine on isolated network; DMZ for gateways | Mandatory |

## 15.10 Observability

| REQ ID | Requirement | Target | Priority |
|--------|------------|--------|----------|
| NFR-OBS-001 | Structured logging | All components emit structured log events (JSON) | Mandatory |
| NFR-OBS-002 | Metrics collection | Latency histograms, throughput counters, error rates per component | Mandatory |
| NFR-OBS-003 | Latency measurement | Hardware-timestamped latency at every processing stage | Mandatory |
| NFR-OBS-004 | Business metrics | Orders/sec, trades/sec, volume, turnover, participants active | Mandatory |
| NFR-OBS-005 | Alert rules | Configurable alerting with severity levels and escalation | Mandatory |
| NFR-OBS-006 | Dashboard | Real-time operational dashboard with < 5 second refresh | Mandatory |
| NFR-OBS-007 | Distributed tracing | Request tracing across components (non-hot-path only) | Highly Recommended |
| NFR-OBS-008 | Log retention | Online: 30 days; archived: 7 years | Mandatory |

## 15.11 Compliance

| REQ ID | Requirement | Target | Priority |
|--------|------------|--------|----------|
| NFR-CMP-001 | Audit trail completeness | 100% of state mutations recorded | Mandatory |
| NFR-CMP-002 | Timestamp accuracy | ≤ 100 µs to UTC (MiFID II); ≤ 50 ms (SEC) | Mandatory |
| NFR-CMP-003 | Data retention | Minimum 5 years for audit data; 7 years for trade data | Mandatory |
| NFR-CMP-004 | Clock sync evidence | PTP sync status logged and auditable | Mandatory |
| NFR-CMP-005 | Regulatory data export | Export audit/trade data in regulator-required formats | Mandatory |

## 15.12 Maintainability and Deployability

| REQ ID | Requirement | Target | Priority |
|--------|------------|--------|----------|
| NFR-MNT-001 | Test coverage — core engine | ≥ 95% line coverage; 100% branch coverage for matching logic | Mandatory |
| NFR-MNT-002 | Test coverage — platform | ≥ 80% line coverage | Mandatory |
| NFR-MNT-003 | CI/CD pipeline duration | < 30 minutes for full build + test | Mandatory |
| NFR-MNT-004 | Rolling upgrade | Non-hot-path components upgradeable without trading interruption | Highly Recommended |
| NFR-MNT-005 | Rollback capability | Any deployment rollbackable within 5 minutes | Mandatory |
| NFR-MNT-006 | Configuration-driven behavior | Existing order types and market models are enabled/disabled/parameterized through configuration without code change. Materially new order semantics or matching algorithms require a controlled software release. | Mandatory |
| NFR-MNT-007 | Backward compatibility | Wire protocol backward-compatible for ≥ 6 months after deprecation | Mandatory |

## 15.13 Technology Recommendations

### 15.13.1 Hot Path Language Selection

| Language | Latency | Throughput | Determinism | Talent Pool | Ecosystem | Recommendation |
|----------|---------|-----------|-------------|-------------|-----------|---------------|
| **C++** (17/20) | Excellent (< 1 µs) | Excellent | Excellent (no GC) | Moderate (specialized) | Mature (exchange domain) | **Recommended for new builds** |
| **Rust** | Excellent (< 1 µs) | Excellent | Excellent (no GC) | Growing (harder to hire) | Growing | **Recommended if team has Rust expertise** |
| **Java** (low-lat tuned) | Good (1–5 µs with tuning) | Excellent | Good (requires GC tuning) | Large | Very mature | **Acceptable with aggressive GC tuning** (Azul Zing/C4, Shenandoah, off-heap) |

**Decision Factors**:
- C++ and Rust provide deterministic memory management without GC pauses
- Java requires significant tuning (off-heap allocation, object pooling, GC avoidance) to approach C++/Rust latency
- C++ has the largest existing talent pool in exchange engineering
- Rust offers stronger memory safety guarantees but smaller talent pool
- Java provides faster development velocity for non-hot-path components

**Recommendation**: C++ or Rust for the matching engine hot path; Java/Kotlin or Go for control plane and platform services.

### 15.13.2 Infrastructure Choices

| Choice | Recommendation | Rationale |
|--------|---------------|-----------|
| **Kernel bypass** | DPDK for gateway networking | Eliminates kernel overhead; essential for < 5 µs gateway latency |
| **io_uring** | Journal writes, non-gateway I/O | Modern async I/O without kernel bypass complexity |
| **NUMA-aware design** | Mandatory for multi-socket hosts | Avoid cross-NUMA memory access latency penalty |
| **CPU pinning** | Mandatory for hot-path threads | Eliminates context switch latency |
| **Memory pre-allocation** | Mandatory for hot path | No allocation in matching path; all memory pre-allocated at startup |
| **NIC timestamping** | Mandatory (Mellanox/Xilinx NICs) | Hardware timestamps for accurate latency measurement and regulatory compliance |
| **PTP synchronization** | Mandatory (IEEE 1588v2) | Nanosecond clock sync across all hosts |
| **Bare metal** | Matching engine, gateways, sequencer | No virtualization overhead for latency-critical components |
| **Containers** | Control plane, operations, monitoring | Operational flexibility for non-latency-critical services |
| **Kubernetes** | Operations services only | NOT for matching engine or gateways |

### 15.13.3 FPGA Considerations

| Aspect | Assessment |
|--------|-----------|
| **Use case** | Sub-microsecond gateway processing, hardware timestamping, ticker plant |
| **Advantage** | Lowest possible latency; deterministic processing |
| **Disadvantage** | High development cost; specialized talent required; difficult to modify; long iteration cycles |
| **Recommendation** | **Do not use for Foundation Release**. Consider for Phase 4+ if latency competition demands it. Software-based approach achieves < 5 µs which is competitive for most markets. |
| **When to choose FPGA** | Only when competing with venues that already use FPGA (e.g., major US equity venues) and sub-microsecond gateway latency is a commercial necessity |

### 15.13.4 Message Bus for Non-Hot-Path

| Option | Use Case | Recommendation |
|--------|----------|---------------|
| **Aeron** | Low-latency messaging between engine and surrounding services | **Recommended** for engine → market data, engine → post-trade |
| **LMAX Disruptor** | Ring-buffer for intra-process communication | **Recommended** within matching engine process |
| **Apache Kafka** | Durable event streaming for surveillance, analytics, reporting | **Recommended** for non-latency-critical event distribution |
| **Redis Streams** | Lightweight event distribution, caching | Acceptable for operational tooling |
| **gRPC** | Request/reply for control plane APIs | **Recommended** for admin/reference data APIs |
