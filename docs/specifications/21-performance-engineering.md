# 21. Performance Engineering and Capacity Planning

## 21.1 Performance Budget

### 21.1.1 End-to-End Latency Budget — Deployment Profile A (Binary Gateway, Bare-Metal Colocation, Kernel Bypass)

```
Component                          Target (p99)    Notes
────────────────────────────────────────────────────────────
NIC receive → gateway process       1 µs           Hardware timestamp → DPDK poll
Gateway validation                  2 µs           Field validation, session check
Gateway → risk service              1 µs           Shared memory or lock-free queue
Risk check                          3 µs           Pre-trade risk evaluation
Risk → sequencer                    1 µs           Lock-free queue
Sequencer + journal write           2 µs           Sequence assign + NVMe fsync
Sequencer → engine                  1 µs           Lock-free queue / Disruptor
Matching engine processing          5 µs           Match + book update + event gen
Engine → output bus                 1 µs           Lock-free queue
Output → gateway response           2 µs           Encode + NIC send
────────────────────────────────────────────────────────────
TOTAL                              19 µs (p99)     Target: < 25 µs wire-to-wire
```

### 21.1.2 Throughput Budget

| Component | Sustained | Burst (10s) |
|-----------|-----------|-------------|
| Single partition matching | 1,000,000 msg/s | 5,000,000 msg/s |
| Journal write | 2,000,000 entries/s | 5,000,000 entries/s |
| Binary gateway (per instance) | 2,000,000 msg/s | 5,000,000 msg/s |
| FIX gateway (per instance) | 500,000 msg/s | 1,000,000 msg/s |
| Market data output (aggregate) | 5,000,000 updates/s | 10,000,000 updates/s |
| Risk service | 2,000,000 checks/s | 5,000,000 checks/s |

## 21.2 Performance Testing Requirements

### 21.2.1 Test Types

| Test | Purpose | Duration | Load Profile |
|------|---------|----------|-------------|
| **Baseline** | Establish performance under normal load | 1 hour | Average daily volume |
| **Peak** | Verify performance at expected peak | 1 hour | 3× average volume |
| **Stress** | Find breaking point | Until degradation | Ramp to failure |
| **Soak** | Detect memory leaks, state growth | 8+ hours | Sustained average load |
| **Burst** | Verify burst handling | 30 minutes | Periodic 10× spikes |
| **Auction** | Test auction under load | Per auction cycle | Maximum auction participation |
| **Recovery** | Measure warm restart under load | Per restart | Full book with pending orders |
| **Failover** | Measure failover time under load | Per failover | Active trading during failure |
| **Fairness** | Verify multi-gateway fairness | 1 hour | Equal load from multiple gateways |

### 21.2.2 Performance Acceptance Criteria

| Metric | Condition | Threshold | Action if Failed |
|--------|-----------|-----------|-----------------|
| p99 matching latency | Under sustained load | Within deployment-profile target (Profile A: < 5 µs; Profile B: < 20 µs) | Investigate and optimize |
| p99.9 matching latency | Under sustained load | ≤ 2× the p99 target for the selected profile | Investigate outliers |
| Jitter (p99 - p50) | Under sustained load | < 5 µs | CPU/memory optimization |
| Throughput | Sustained | ≥ 1M msg/s per partition | Architecture review |
| Memory growth | 8-hour soak | < 1% growth after warmup | Memory leak investigation |
| GC pauses (if JVM) | Under load | No GC on hot path (off-heap allocation); if GC is present in non-hot-path JVM components, target p99 < 500 µs | Off-heap allocation or GC-free language for hot path |
| Journal write-to-storage latency | Under load, Regulatory-Grade sync mode | < 2 µs (p99); < 500 ns achievable only in Reduced-Latency batched mode | I/O optimization |
| Gateway fairness delta | Multi-gateway test | Per deployment profile: < 1 µs (hardware sequencer), < 5 µs (software time-based), < 50 µs (Profile C) | Sequencing configuration review |

### 21.2.3 Performance Test Infrastructure
- Dedicated performance test environment mirroring production hardware
- Load generator capable of sustained 10M+ msg/s
- Hardware timestamping for accurate latency measurement
- Automated performance regression testing in CI/CD (daily)
- Performance dashboard with historical trend analysis

## 21.3 Optimization Techniques

### 21.3.1 Memory Optimization
- **Pre-allocation**: All order book memory allocated at startup; no allocation in hot path
- **Object pooling**: Reuse order, execution, and event objects from pre-allocated pools
- **Cache-friendly layout**: Orders at same price level in contiguous memory
- **Hugepages**: 2MB or 1GB hugepages for reduced TLB misses
- **NUMA-local allocation**: Memory allocated on same NUMA node as processing CPU

### 21.3.2 CPU Optimization
- **CPU pinning**: Hot-path threads pinned to dedicated cores
- **Isolcpus**: Kernel isolates pinned cores from scheduler
- **Busy-spin**: Hot-path threads poll (not sleep/wait) for new messages
- **Branch prediction**: Order processing paths optimized for common case
- **SIMD**: Vectorized operations where applicable (price comparisons, checksum)

### 21.3.3 I/O Optimization
- **Kernel bypass**: DPDK for gateway networking
- **io_uring**: Asynchronous journal writes without kernel overhead
- **Direct I/O**: Bypass page cache for journal (application manages caching)
- **Write batching**: Available only in Reduced-Latency durability mode. Batch journal entries within a configurable window (e.g., 10–100 µs). Not available in Regulatory-Grade mode where each entry must be durably written before processing. See §11.3 and §27.2.1 for mode definitions and venue-type applicability.
- **Zero-copy**: Avoid data copies between kernel and user space

### 21.3.4 Lock-Free Design
- **Single-threaded per partition**: No locks needed within matching engine
- **Lock-free queues**: Inter-thread communication via SPSC (single-producer-single-consumer) ring buffers
- **Atomic operations**: Where shared state is unavoidable, use CAS operations
- **No mutex/semaphore**: Eliminated from all hot-path code

## 21.4 Performance Monitoring (Production)

### 21.4.1 Real-Time Metrics

| Metric | Granularity | Alert Threshold |
|--------|-------------|----------------|
| Matching latency p50/p99/p99.9 | Per partition, 1-second buckets | p99 > 2× baseline |
| Gateway round-trip p50/p99 | Per gateway instance, 1-second buckets | p99 > 2× baseline |
| Message throughput | Per partition, per second | < 50% of capacity (investigate) |
| Journal write latency | Per partition, 1-second buckets | p99 > 1 µs |
| Queue depths | Per inter-thread queue | > 80% capacity |
| Memory usage | Per process | > 80% allocated |
| CPU utilization | Per pinned core | > 90% sustained |
| Context switches | Per pinned core | > 10/second (indicates misconfiguration) |
| Cache miss rate | Per core (perf counters) | > 5% L3 miss rate |
| Network packet drops | Per NIC | Any drops = alert |
| PTP clock offset | Per host | > 500 ns from grandmaster |
