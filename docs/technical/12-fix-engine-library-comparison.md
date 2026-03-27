# FIX Engine Library Comparison — Decision Support

**Status**: Evaluation artifact — all options are candidates, not approved selections
**Purpose**: Support FIX engine library selection for the FR-1 FIX gateway

---

## Candidate Options

### Option A: QuickFIX/J (Open Source, Java)

| Factor | Assessment |
|--------|-----------|
| **Language** | Java (native). C++ version (QuickFIX) also exists but less maintained. |
| **License** | BSD-style open source |
| **FIX versions** | 4.0 through 5.0SP2 |
| **Maturity** | Very high — most widely deployed open-source FIX engine |
| **Session management** | Built-in session handling, heartbeat, sequence management, gap recovery |
| **Performance** | Moderate — adequate for FIX gateway (not hot-path); ~10K msg/s per session typical |
| **Customization** | Message factory, data dictionary customization, custom tags supported |
| **Community** | Large community; many examples; well-documented |
| **Suitability for FR-1** | Strong if Java is the gateway language or if gateway is JVM-based |
| **Pros** | Proven; large community; free; comprehensive session handling |
| **Cons** | Java dependency; GC considerations; not suitable if gateway must be C++/Rust-native |
| **Recommendation condition** | Use if the gateway layer runs on JVM (even if engine is C++/Rust) |

### Option B: QuickFIX (Open Source, C++)

| Factor | Assessment |
|--------|-----------|
| **Language** | C++ (native) |
| **License** | BSD-style open source |
| **FIX versions** | 4.0 through 4.4 (5.0 support limited) |
| **Maturity** | High but less actively maintained than QuickFIX/J |
| **Performance** | Good — native C++; lower overhead than JVM |
| **Customization** | Data dictionary XML; custom tags via extension |
| **Suitability for FR-1** | Strong if C++ is the gateway language |
| **Pros** | Native C++; no JVM dependency; proven |
| **Cons** | Less maintained; FIX 5.0 support weaker; smaller community than /J |
| **Recommendation condition** | Use if C++ is the hot-path and gateway language and FIX 4.4 is sufficient for FR-1 |

### Option C: Custom FIX Stack

| Factor | Assessment |
|--------|-----------|
| **Language** | Any (built from scratch) |
| **License** | Owned |
| **FIX versions** | Whatever is implemented |
| **Maturity** | Zero — must be built |
| **Performance** | Can be optimized for specific workload |
| **Customization** | Complete control |
| **Suitability for FR-1** | Risky — FIX is complex; session management, gap recovery, sequence handling are non-trivial |
| **Pros** | Full control; no dependency; can optimize for specific venue profile |
| **Cons** | High development cost (3–6 months for a solid FIX engine); high bug risk; reinventing proven wheel |
| **Recommendation condition** | Only if existing libraries are fundamentally unsuitable (unlikely for FR-1) |

### Option D: Commercial FIX Engine (e.g., OnixS, B2BITS, Chronicle FIX)

| Factor | Assessment |
|--------|-----------|
| **Language** | Varies (Java, C++, or both) |
| **License** | Commercial — annual license fee |
| **FIX versions** | Comprehensive (4.0 through FIXT/5.0SP2) |
| **Maturity** | Very high — exchange-grade |
| **Performance** | High — optimized for low-latency trading |
| **Support** | Vendor support; SLAs; patches |
| **Suitability for FR-1** | Strong if budget allows |
| **Pros** | Proven at exchanges; vendor support; low integration risk |
| **Cons** | License cost ($20K–$100K+/year); vendor dependency |
| **Recommendation condition** | Consider if team lacks FIX expertise; budget permits; time-to-market is critical |

## Comparison Matrix

| Factor | QuickFIX/J | QuickFIX (C++) | Custom | Commercial |
|--------|:----------:|:--------------:|:------:|:----------:|
| Cost | Free | Free | Dev effort | $20K–100K+/yr |
| Time to integrate | 2–4 weeks | 2–4 weeks | 3–6 months | 2–4 weeks |
| FIX 4.4 support | Excellent | Good | Must build | Excellent |
| FIX 5.0SP2 | Excellent | Limited | Must build | Excellent |
| C++ compatibility | No (JVM) | Native | Native | Varies |
| Rust compatibility | No | FFI possible | Native | No |
| Java compatibility | Native | JNI possible | N/A | Varies |
| Session handling | Built-in | Built-in | Must build | Built-in |
| Gap recovery | Built-in | Built-in | Must build | Built-in |
| Custom tags | Supported | Supported | Full control | Supported |
| Exchange-grade proven | Many firms | Many firms | Unproven | Exchanges |
| FR-1 risk level | Low | Low | High | Low |

## Provisional Recommendation

| If D-01 = ... | Recommended FIX approach |
|:-------------:|------------------------|
| **C++** | QuickFIX (C++) for FR-1 (FIX 4.4 sufficient). Evaluate commercial option if FIX 5.0SP2 needed in Phase 2. |
| **Rust** | QuickFIX (C++) via FFI, or QuickFIX/J if gateway is a separate JVM service. |
| **Java** | QuickFIX/J — natural fit; most proven; largest community. |

**Key insight**: The FIX gateway does not need to be the same language as the matching engine. A JVM-based FIX gateway can front-end a C++/Rust matching engine via the internal protocol (SBE over shared memory or Aeron).

*All named products are candidate options for evaluation, not approved selections.*
