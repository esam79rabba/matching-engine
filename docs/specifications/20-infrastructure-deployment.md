# 20. Infrastructure and Deployment Architecture

## 20.1 Reference Deployment Architecture

### 20.1.1 Colocation Deployment (Primary)

```
┌─────────────────────────────────────────────────────────────────────┐
│                     DATA CENTER — PRIMARY SITE                       │
│                                                                      │
│  ┌─────────────────────────────────┐  ┌────────────────────────────┐│
│  │  MATCHING ENGINE RACK            │  │  GATEWAY RACK              ││
│  │                                   │  │                            ││
│  │  Server 1: Engine Partitions 1-4  │  │  Server 5: FIX GW (active)││
│  │  Server 2: Engine Partitions 5-8  │  │  Server 6: FIX GW (stby)  ││
│  │  Server 3: Engine (hot standby)   │  │  Server 7: Binary GW (act)││
│  │  Server 4: Sequencer (act+stby)   │  │  Server 8: Binary GW (stby)│
│  │                                   │  │  Server 9: WS/REST GW     ││
│  │  PTP Grandmaster (GPS)            │  │  Server 10: MD Publisher   ││
│  │  NVMe Journal Storage (RAID)      │  │                            ││
│  └─────────────────────────────────┘  └────────────────────────────┘│
│                                                                      │
│  ┌─────────────────────────────────┐  ┌────────────────────────────┐│
│  │  PLATFORM SERVICES RACK          │  │  INFRASTRUCTURE RACK       ││
│  │                                   │  │                            ││
│  │  Server 11: Risk Service          │  │  PTP Switch / Grandmaster ││
│  │  Server 12: Reference Data        │  │  Network Switches (10/25G)││
│  │  Server 13: Post-Trade            │  │  Multicast switches       ││
│  │  Server 14: Ops Console + Admin   │  │  Firewall / Load Balancer ││
│  │  Server 15: Surveillance Export   │  │  NTP/PTP appliances       ││
│  │  Server 16: Monitoring            │  │  HSM                      ││
│  └─────────────────────────────────┘  └────────────────────────────┘│
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────────┐│
│  │  PARTICIPANT CO-LOCATION CAGE                                     ││
│  │  Cross-connects to gateway switches                               ││
│  └─────────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────────┘
```

### 20.1.2 Hardware Specifications (Reference)

**Matching Engine Server**:
| Component | Specification |
|-----------|--------------|
| CPU | AMD EPYC 9654 (96 cores) or Intel Xeon w9-3595X (60 cores, high frequency) |
| RAM | 256 GB DDR5 ECC (pre-allocated, no swap) |
| Storage | 2× 3.84TB NVMe SSD (RAID-1 for journal) |
| NIC | Mellanox ConnectX-7 (100GbE, hardware timestamping, DPDK support) |
| NUMA | Single socket preferred (avoid cross-NUMA penalty) or dual-socket with NUMA-aware pinning |

**Gateway Server**:
| Component | Specification |
|-----------|--------------|
| CPU | Intel Xeon Gold 6448H (32 cores, high frequency) |
| RAM | 128 GB DDR5 |
| Storage | 1TB NVMe SSD |
| NIC | Mellanox ConnectX-7 (100GbE, hardware timestamping, DPDK) |

**Network**:
| Component | Specification |
|-----------|--------------|
| Core switches | 100GbE, cut-through switching, < 500ns switching latency |
| Multicast | IGMP snooping, PIM for multicast market data |
| PTP | IEEE 1588v2 boundary clocks; GPS grandmaster |
| Firewall | Stateful inspection; < 5µs latency for colocation traffic |

## 20.2 Linux Tuning

### 20.2.1 Kernel Parameters

```bash
# Disable CPU frequency scaling
echo performance > /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Disable C-states (prevent CPU sleep)
processor.max_cstate=0 intel_idle.max_cstate=0

# Disable transparent hugepages (use explicit hugepages)
echo never > /sys/kernel/mm/transparent_hugepage/enabled

# Pre-allocate hugepages (2MB pages)
echo 4096 > /proc/sys/vm/nr_hugepages

# Disable NUMA balancing (use explicit pinning)
echo 0 > /proc/sys/kernel/numa_balancing

# Network tuning
net.core.rmem_max = 134217728
net.core.wmem_max = 134217728
net.core.netdev_max_backlog = 300000
net.core.busy_poll = 50
net.core.busy_read = 50

# Disable swap
vm.swappiness = 0
```

### 20.2.2 CPU Pinning Strategy

```
Core 0:     OS / interrupts (isolate from application)
Core 1:     Sequencer thread (partition 1)
Core 2:     Matching engine thread (partition 1)
Core 3:     Journal writer thread (partition 1)
Core 4-6:   Partition 2 (sequencer, engine, journal)
Core 7-9:   Partition 3
Core 10-12: Partition 4
Core 13:    Market data publisher
Core 14:    Gateway receive thread (binary)
Core 15:    Gateway send thread (binary)
Core 16-17: FIX gateway threads
Core 18-19: Risk service threads
Core 20+:   Platform services (not pinned, shared)
```

### 20.2.3 DPDK Configuration
- Hugepage-backed memory for packet buffers
- Poll-mode drivers (PMD) for NIC — no interrupt overhead
- Dedicated cores for packet processing
- User-space networking stack bypasses kernel TCP/IP

## 20.3 Hybrid Deployment (Cloud + Bare Metal)

For venues that do not require ultra-low latency or as a cost-effective starting point:

```
BARE METAL (Colocation)              CLOUD (Kubernetes)
┌──────────────────────┐            ┌──────────────────────────┐
│ Matching Engine       │            │ Reference Data Service    │
│ Sequencer             │            │ Post-Trade Service        │
│ Binary Gateway        │            │ Operations Console        │
│ Market Data Publisher  │            │ Participant Admin         │
│ Journal Storage        │─── VPN ──→│ Monitoring / Observability│
└──────────────────────┘            │ Surveillance Export       │
                                     │ Certification Environment │
                                     │ CI/CD Pipeline            │
                                     └──────────────────────────┘
```

**Boundary Rule**: Anything in the hot path or with < 100µs latency requirement runs on bare metal. Everything else can run in containers/cloud.

## 20.4 Capacity Planning

### 20.4.1 Sizing Model

| Dimension | Calculation | Example (Medium Exchange) |
|-----------|------------|--------------------------|
| Partitions needed | total_instruments / instruments_per_partition | 5,000 / 1,000 = 5 partitions |
| Engine hosts | partitions / partitions_per_host | 5 / 4 = 2 hosts (+ 1 standby) |
| Gateway hosts | peak_sessions / sessions_per_gateway | 200 / 100 = 2 FIX GW pairs |
| Journal storage (daily) | msg_rate × msg_size × trading_hours | 500K/s × 100B × 28,800s = 1.4 TB/day |
| Journal storage (90 days) | daily × 90 | 126 TB (warm storage) |
| Market data bandwidth | updates/s × msg_size × subscribers | 1M/s × 50B × 200 = 10 Gbps |

### 20.4.2 Growth Planning
- Design for 3× current peak capacity
- Horizontal scaling (add partitions/hosts) for instrument growth
- Gateway scaling (add instances) for session growth
- Journal storage scales linearly with message rate
- Annual capacity review and refresh cycle
