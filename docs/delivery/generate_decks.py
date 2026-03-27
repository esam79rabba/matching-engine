#!/usr/bin/env python3
"""Generate Executive Summary and Architecture Review PowerPoint decks."""

from pptx import Presentation
from pptx.util import Inches, Pt, Emu
from pptx.dml.color import RGBColor
from pptx.enum.text import PP_ALIGN, MSO_ANCHOR
import os

OUT_DIR = os.path.dirname(os.path.abspath(__file__))

# ─── Color palette ───────────────────────────────────────────
DARK_NAVY = RGBColor(0x1A, 0x1A, 0x2E)
BLUE = RGBColor(0x1A, 0x23, 0x7E)
RED = RGBColor(0xCC, 0x00, 0x00)
GREEN = RGBColor(0x2E, 0x7D, 0x32)
AMBER = RGBColor(0xD6, 0xAE, 0x00)
GRAY = RGBColor(0x66, 0x66, 0x66)
LIGHT_GRAY = RGBColor(0x99, 0x99, 0x99)
WHITE = RGBColor(0xFF, 0xFF, 0xFF)
BG_LIGHT = RGBColor(0xF5, 0xF5, 0xF5)


def set_slide_bg(slide, color):
    bg = slide.background
    fill = bg.fill
    fill.solid()
    fill.fore_color.rgb = color


def add_title_slide(prs, title, subtitle, note=""):
    slide = prs.slides.add_slide(prs.slide_layouts[6])  # blank
    set_slide_bg(slide, DARK_NAVY)
    # Title
    txBox = slide.shapes.add_textbox(Inches(1), Inches(2.5), Inches(8), Inches(1))
    tf = txBox.text_frame
    tf.word_wrap = True
    p = tf.paragraphs[0]
    p.text = title
    p.font.size = Pt(32)
    p.font.bold = True
    p.font.color.rgb = WHITE
    p.alignment = PP_ALIGN.CENTER
    # Subtitle
    txBox2 = slide.shapes.add_textbox(Inches(1), Inches(3.6), Inches(8), Inches(0.8))
    tf2 = txBox2.text_frame
    tf2.word_wrap = True
    p2 = tf2.paragraphs[0]
    p2.text = subtitle
    p2.font.size = Pt(14)
    p2.font.color.rgb = LIGHT_GRAY
    p2.alignment = PP_ALIGN.CENTER
    if note:
        notes_slide = slide.notes_slide
        notes_slide.notes_text_frame.text = note
    return slide


def add_content_slide(prs, title, bullets, note="", diagram_ref="", assumption_banner=False):
    slide = prs.slides.add_slide(prs.slide_layouts[6])  # blank
    set_slide_bg(slide, WHITE)

    # Title bar
    title_box = slide.shapes.add_textbox(Inches(0.5), Inches(0.3), Inches(9), Inches(0.6))
    tf = title_box.text_frame
    tf.word_wrap = True
    p = tf.paragraphs[0]
    p.text = title
    p.font.size = Pt(22)
    p.font.bold = True
    p.font.color.rgb = DARK_NAVY

    # Planning assumption banner
    start_y = 1.1
    if assumption_banner:
        banner = slide.shapes.add_textbox(Inches(0.5), Inches(0.95), Inches(9), Inches(0.3))
        btf = banner.text_frame
        bp = btf.paragraphs[0]
        bp.text = "Planning assumptions - not approved commitments"
        bp.font.size = Pt(9)
        bp.font.italic = True
        bp.font.color.rgb = AMBER
        start_y = 1.3

    # Bullets
    body_box = slide.shapes.add_textbox(Inches(0.7), Inches(start_y), Inches(5.3), Inches(4.5))
    tf2 = body_box.text_frame
    tf2.word_wrap = True
    for i, bullet in enumerate(bullets):
        if i == 0:
            p = tf2.paragraphs[0]
        else:
            p = tf2.add_paragraph()
        # Handle bold prefix (text before colon)
        if ": " in bullet and bullet.index(": ") < 40:
            bold_part, rest = bullet.split(": ", 1)
            run1 = p.add_run()
            run1.text = bold_part + ": "
            run1.font.bold = True
            run1.font.size = Pt(13)
            run1.font.color.rgb = DARK_NAVY
            run2 = p.add_run()
            run2.text = rest
            run2.font.size = Pt(13)
            run2.font.color.rgb = GRAY
        else:
            p.text = bullet
            p.font.size = Pt(13)
            p.font.color.rgb = GRAY
        p.space_after = Pt(6)
        p.level = 0

    # Diagram reference callout
    if diagram_ref:
        diag_box = slide.shapes.add_textbox(Inches(6.3), Inches(1.3), Inches(3.2), Inches(3.5))
        dtf = diag_box.text_frame
        dtf.word_wrap = True
        dp = dtf.paragraphs[0]
        dp.text = f"[INSERT DIAGRAM]\n{diagram_ref}"
        dp.font.size = Pt(10)
        dp.font.color.rgb = LIGHT_GRAY
        dp.alignment = PP_ALIGN.CENTER

    # Speaker notes
    if note:
        notes_slide = slide.notes_slide
        notes_slide.notes_text_frame.text = note

    return slide


def add_table_slide(prs, title, headers, rows, note="", assumption_banner=False):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    set_slide_bg(slide, WHITE)

    # Title
    title_box = slide.shapes.add_textbox(Inches(0.5), Inches(0.3), Inches(9), Inches(0.6))
    tf = title_box.text_frame
    p = tf.paragraphs[0]
    p.text = title
    p.font.size = Pt(22)
    p.font.bold = True
    p.font.color.rgb = DARK_NAVY

    start_y = 1.1
    if assumption_banner:
        banner = slide.shapes.add_textbox(Inches(0.5), Inches(0.95), Inches(9), Inches(0.3))
        btf = banner.text_frame
        bp = btf.paragraphs[0]
        bp.text = "Planning assumptions - not approved commitments"
        bp.font.size = Pt(9)
        bp.font.italic = True
        bp.font.color.rgb = AMBER
        start_y = 1.3

    cols = len(headers)
    tbl_rows = len(rows) + 1
    col_width = 9.0 / cols
    table_shape = slide.shapes.add_table(tbl_rows, cols, Inches(0.5), Inches(start_y),
                                          Inches(9), Inches(min(tbl_rows * 0.45, 5.0)))
    table = table_shape.table

    # Header row
    for i, h in enumerate(headers):
        cell = table.cell(0, i)
        cell.text = h
        for paragraph in cell.text_frame.paragraphs:
            paragraph.font.size = Pt(10)
            paragraph.font.bold = True
            paragraph.font.color.rgb = WHITE
        cell.fill.solid()
        cell.fill.fore_color.rgb = DARK_NAVY

    # Data rows
    for r, row_data in enumerate(rows):
        for c, val in enumerate(row_data):
            cell = table.cell(r + 1, c)
            cell.text = str(val)
            for paragraph in cell.text_frame.paragraphs:
                paragraph.font.size = Pt(9)
                paragraph.font.color.rgb = GRAY

    if note:
        notes_slide = slide.notes_slide
        notes_slide.notes_text_frame.text = note

    return slide


def add_section_divider(prs, title, subtitle=""):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    set_slide_bg(slide, BLUE)
    txBox = slide.shapes.add_textbox(Inches(1), Inches(3), Inches(8), Inches(1))
    tf = txBox.text_frame
    p = tf.paragraphs[0]
    p.text = title
    p.font.size = Pt(28)
    p.font.bold = True
    p.font.color.rgb = WHITE
    p.alignment = PP_ALIGN.CENTER
    if subtitle:
        txBox2 = slide.shapes.add_textbox(Inches(1), Inches(4), Inches(8), Inches(0.5))
        tf2 = txBox2.text_frame
        p2 = tf2.paragraphs[0]
        p2.text = subtitle
        p2.font.size = Pt(14)
        p2.font.color.rgb = LIGHT_GRAY
        p2.alignment = PP_ALIGN.CENTER
    return slide


# ═══════════════════════════════════════════════════════════════
# DECK 1: EXECUTIVE SUMMARY
# ═══════════════════════════════════════════════════════════════
def create_executive_deck():
    prs = Presentation()
    prs.slide_width = Inches(10)
    prs.slide_height = Inches(7.5)

    # Slide 1: Title
    add_title_slide(prs,
        "Multi-Asset Matching Engine Platform",
        "Executive Briefing  |  Confidential  |  2026-03-24\nPlanning assumptions - not approved commitments until stakeholder sign-off",
        "Standard title slide. Context: go/no-go briefing for Foundation Release (FR-1).")

    # Slide 2: Opportunity
    add_content_slide(prs,
        "The Market Opportunity",
        [
            "Electronic trading venue infrastructure is a large, growing market with aging incumbent technology",
            "Many exchanges operate matching platforms built over a decade ago with high vendor lock-in",
            "Regulatory fragmentation across jurisdictions creates demand for compliance-ready venue technology",
            "New asset classes (carbon, digital assets, tokenized securities) need institutional-grade infrastructure",
            "Strategic thesis: no available platform meets performance, multi-asset, compliance, and cost requirements simultaneously",
        ],
        "Frame as strategic thesis, not verified market research. Specific market-size figures should be sourced from independent research if used in investor materials.",
        "")

    # Slide 3: Solution
    add_content_slide(prs,
        "What We Are Building",
        [
            "Configurable, multi-asset electronic trading venue platform",
            "Deterministic matching engine designed for institutional-grade performance",
            "Compliance-by-configuration: deploy across jurisdictions without re-architecture",
            "Full platform: connectivity, risk controls, market data, post-trade, surveillance hooks",
            "Owned technology: no vendor lock-in, full commercial flexibility",
        ],
        "Not just a matching engine - a complete venue platform. Emphasize 'configurable' and 'multi-jurisdiction'.",
        "Diagram: 01-system-context.drawio (simplified)")

    # Slide 4: Target Markets
    add_content_slide(prs,
        "Who It Serves",
        [
            "Regulated exchanges (national / regional)",
            "MTFs, OTFs, and alternative trading systems",
            "Digital-asset exchanges seeking institutional credibility",
            "Commodity, energy, and carbon trading venues",
            "Regional multi-asset market operators",
            "Broker internal crossing networks",
        ],
        "Same platform technology serves all venue types through configuration. 'Build once, deploy many.'",
        "")

    # Slide 5: Design Principles
    add_content_slide(prs,
        "Key Design Principles",
        [
            "Determinism: identical input always produces identical output - essential for audit and replay",
            "Separation of concerns: hot-path matching isolated from control plane and operations",
            "Configurability: existing market models parameterized; new logic via controlled release",
            "Resilience by design: journal, snapshots, warm restart, defined RTO/RPO",
            "Regulatory readiness: audit trails, clock sync, surveillance hooks built in from inception",
        ],
        "These principles are non-negotiable and guide every design decision.",
        "")

    # Slide 6: FR-1 vs Enterprise
    add_content_slide(prs,
        "Foundation Release (FR-1) vs. Enterprise Target State",
        [
            "FR-1: Core matching, basic risk, FIX + binary connectivity, market data, audit trail",
            "Enterprise: Multi-asset, advanced orders, RFQ, DR, clearing, full surveillance",
            "FR-1 supports initial licensing engagement, subject to jurisdiction-specific approval",
            "Each phase is additive - no re-architecture required",
            "FR-1 is a production-capable release, not a prototype",
        ],
        "FR-1 can run a trading day and generate revenue. Enterprise features are additive.",
        "Diagram: 06-fr1-vs-enterprise.drawio")

    # Slide 7: Deployment Profiles
    add_content_slide(prs,
        "Deploy Anywhere - Three Performance Profiles",
        [
            "Profile A: Ultra-low-latency colocation - for tier-1 exchanges competing on speed",
            "Profile B: Standard exchange-grade - for most regulated venues",
            "Profile C: Cloud-compatible - for startups, broker crossing, commodity/carbon",
            "Same platform code; infrastructure choices determine performance tier",
            "All performance figures are engineering targets, not contractual guarantees",
        ],
        "Key commercial differentiator: same platform serves an HFT equity exchange and a carbon credit startup.",
        "Diagram: 07-deployment-profiles.drawio (simplified)")

    # Slide 8: Roadmap
    add_table_slide(prs,
        "Path to Production",
        ["Phase", "Name", "Scope", "Timeline Target"],
        [
            ["1", "Foundation Release (FR-1)", "Core matching, basic risk, FIX/binary, market data", "9 months (target)"],
            ["2", "Exchange-Grade (EGR)", "Multi-asset, clearing, full surveillance, DR", "Months 6-15"],
            ["3", "Multi-Asset (MAR)", "RFQ, dark pools, complex orders", "Months 12-21"],
            ["4", "Advanced Ops (AOR)", "Automated DR, chaos testing, optimization", "Months 18-27"],
            ["5", "Ecosystem (ECO)", "Analytics, licensing, white-label", "Months 24-36"],
        ],
        "5-phase approach manages risk: FR-1 proves technology, Phase 2 proves commercial viability.",
        assumption_banner=True)

    # Slide 9: Team and Investment
    add_table_slide(prs,
        "What It Takes (Planning Assumptions)",
        ["Dimension", "Planning Estimate", "Notes"],
        [
            ["Team", "12-15 people", "8 eng + 2 QA + 1 product + 2 SRE + 1 PM"],
            ["Hardware", "$200K-500K range", "Servers, NICs, PTP, switches"],
            ["Software/Licensing", "$50K-100K range", "FIX engine, monitoring, HSM"],
            ["Data Center", "$10K-30K/month", "Colocation"],
            ["Timeline", "9 months from decision closure", "Planning target"],
        ],
        "All figures are planning assumptions subject to decision closure and scope confirmation.",
        assumption_banner=True)

    # Slide 10: Competitive Advantage
    add_content_slide(prs,
        "Why This Platform Wins",
        [
            "Technology sovereignty: no vendor lock-in; own the core technology",
            "Multi-asset from the architecture: designed multi-asset from day one, not extended later",
            "Compliance-by-configuration: same platform, different jurisdictions, no re-build",
            "Modern architecture: event-sourced, deterministic, replayable - built for audit",
            "Speed to market: new instruments in weeks using existing capabilities",
        ],
        "Competitive moat is the combination of configurability + performance + compliance readiness.",
        "")

    # Slide 11: Decisions Needed
    add_table_slide(prs,
        "Key Decisions for This Board",
        ["Decision", "Options", "Owner", "Deadline"],
        [
            ["Asset class for FR-1", "Equities / Derivatives / Digital / Carbon", "Product + CTO", "Week 1"],
            ["Target jurisdiction", "EU / US / Singapore / UAE", "Compliance", "Week 1"],
            ["Engine language", "C++ / Rust / Java", "CTO", "Week 2"],
            ["Budget approval", "Approve FR-1 build budget", "Board", "This meeting"],
        ],
        "These decisions must close within 2 weeks to hold the 9-month timeline target.",
        assumption_banner=False)

    # Slide 12: Risk Management
    add_content_slide(prs,
        "Controlled Risk Through Phased Delivery",
        [
            "Phased investment: FR-1 limits initial spend; revenue possible before full platform cost",
            "Technology risk: mitigated by proven architecture patterns and early performance validation",
            "Market risk: pilot participants committed before go-live; market-maker incentives",
            "Regulatory risk: early regulator engagement; compliance-by-design, not bolt-on",
            "Execution risk: clear milestones, monthly decision checkpoints, experienced team",
        ],
        "Every risk has a named mitigation. Phased delivery means we can pivot after FR-1.",
        "")

    # Slide 13: Recommendation
    add_content_slide(prs,
        "Recommendation",
        [
            "APPROVE Foundation Release (FR-1) build with 12-15 person team",
            "AUTHORIZE hardware and colocation budget ($300K-600K planning range)",
            "EMPOWER CTO + Product + Compliance to close top 6 decisions within 2 weeks",
            "SCHEDULE monthly progress reviews against 20-milestone plan",
            "TARGET: FR-1 production-ready in 9 months from decision closure",
        ],
        "Be specific about what approval means: budget, team, timeline, and decision authority. Offer architecture review deck as follow-up.",
        "",
        assumption_banner=True)

    # Slide 14: End
    add_title_slide(prs,
        "Thank You",
        "Next step: Decision workshop within one week\nArchitecture review deck available for technical leadership",
        "")

    path = os.path.join(OUT_DIR, "Matching-Engine-Executive-Summary-v1.0.pptx")
    prs.save(path)
    print(f"Created: {path}")
    return path


# ═══════════════════════════════════════════════════════════════
# DECK 2: ARCHITECTURE REVIEW
# ═══════════════════════════════════════════════════════════════
def create_architecture_deck():
    prs = Presentation()
    prs.slide_width = Inches(10)
    prs.slide_height = Inches(7.5)

    # --- Slide 1: Title ---
    add_title_slide(prs,
        "Matching Engine Platform\nArchitecture Review",
        "Frozen Baseline v1.0.0-FINAL  |  2026-03-24  |  Confidential",
        "Architecture review of the frozen baseline. Open decisions are marked explicitly.")

    # --- Slide 2: Principles ---
    add_content_slide(prs,
        "Core Design Principles",
        [
            "Determinism: identical input produces identical output, always",
            "Separation of concerns: hot path isolated from control plane",
            "Configurability: existing models parameterized; new logic requires controlled software release",
            "Resilience by design: journal, snapshots, warm restart, defined RTO/RPO",
            "Regulatory readiness: audit trails, clock sync, surveillance hooks from inception",
        ],
        "Non-negotiable principles. Every design decision is tested against these.",
        "")

    # --- Slide 3: System Context ---
    add_content_slide(prs,
        "System Context - External Actors",
        [
            "Participants: market makers, brokers, buy-side (DMA), retail intermediaries",
            "Post-trade: CCP (clearing), custodian (settlement)",
            "Regulatory: surveillance system (external), regulator/supervisor",
            "Operations: market ops staff, SRE/infrastructure team",
            "All interactions through defined protocol interfaces (FIX, SBE, REST, multicast)",
        ],
        "Outside-in view. Everything inside the platform boundary is our system.",
        "Diagram: 01-system-context.drawio (full)")

    # --- Slide 4: Logical Architecture ---
    add_content_slide(prs,
        "5-Layer Logical Architecture",
        [
            "Layer 1 (Hot Path): Sequencer > Journal > Engine > Output Bus",
            "Layer 2 (Control Plane): Config, state machine, snapshots, feature flags",
            "Layer 3 (Platform Services): Gateways, risk, market data, post-trade, ref data",
            "Layer 4 (Operations): Ops console, admin, replay, reconciliation, monitoring",
            "Layer 5 (Ecosystem): Clearing, settlement, surveillance, regulatory reporting",
        ],
        "Critical insight: Layers 1-2 are engine-owned and latency-critical. Layers 3-5 are standard services.",
        "Diagram: 02-logical-architecture.drawio (full)")

    # --- Slide 5: Hot Path Boundary ---
    add_content_slide(prs,
        "Hot Path vs Control Plane",
        [
            "Hot path: sequencer through output bus. Single-threaded per partition.",
            "Constraints: no GC, no allocation, no external I/O except journal, no locks",
            "Control plane: everything else. Standard networking, containers, databases OK.",
            "Risk checks execute BEFORE sequencer - latency-sensitive but not in deterministic path",
            "Boundary rule: hot path begins at sequencer, ends at output bus",
        ],
        "Most important boundary in the system. Any violation directly degrades latency and jitter.",
        "Diagram: 04-hot-path-separation.drawio (full)")

    # --- Slide 6: Partition Model ---
    add_content_slide(prs,
        "Single-Threaded Per Partition",
        [
            "Each partition: 1 dedicated CPU core, 1 thread, 1+ order books (instruments)",
            "Deterministic: no locks, no shared mutable state, no context switches on hot path",
            "Horizontal scaling: add partitions for more instruments (more cores)",
            "No cross-partition matching: each instrument belongs to exactly one partition",
            "High-activity instruments may be isolated to a dedicated partition",
        ],
        "Foundational architecture choice. Simplicity and provable determinism over multi-threaded throughput.",
        "")

    # --- Slide 7: Sequencing and Fairness ---
    add_content_slide(prs,
        "Sequencing Boundary and Fairness Model",
        [
            "Sequencing: partition-local monotonic. Global ID = partition_id:local_sequence",
            "Fairness guarantees apply at the venue-controlled sequencing boundary",
            "Network variability outside venue boundary is NOT guaranteed",
            "Targets: < 1 us (HW seq), < 5 us (SW time-based), < 50 us (Profile C)",
            "[OPEN DECISION D-04: hardware vs. software sequencer]",
        ],
        "Fairness boundary is the sequencer. Anything before it varies by access tier.",
        "Diagram: 09-fairness-sequencing-boundary.drawio (full)")

    # --- Slide 8: Order Lifecycle ---
    add_content_slide(prs,
        "Order Lifecycle - End to End",
        [
            "Submit > Gateway validate > Risk check > Sequencer (seq# + timestamp)",
            "Journal persist (Regulatory-Grade: fsync before processing)",
            "Engine match > Output bus > Market data + Drop copy + Post-trade + Surveillance",
            "Rejection path: Risk REJECT > Gateway > Participant (no sequencer involvement)",
            "Market order residual: cancel (default) or market-to-limit with collar",
        ],
        "Journal write happens BEFORE engine processes. This is the zero-data-loss guarantee.",
        "Diagram: 08-order-lifecycle-sequence.drawio (full)")

    # --- Slide 9: Market Order Residual ---
    add_content_slide(prs,
        "Market-Order Residual Policy (Frozen)",
        [
            "When a market order exhausts liquidity and residual remains:",
            "Option 1: Cancel residual (default policy)",
            "Option 2: Convert to market-to-limit with defined protection collar",
            "NOT SUPPORTED: unprotected resting at last fill price",
            "Policy is venue-configurable within this supported set",
        ],
        "Frozen policy. No unprotected last-fill-price resting due to thin-book risk.",
        "")

    # --- Slide 10: Matching Algorithm ---
    add_content_slide(prs,
        "Matching - Price-Time FIFO (FR-1)",
        [
            "FR-1: price-time (FIFO) priority only",
            "Enterprise: adds pro-rata, parity, hybrid - selectable from implemented models",
            "New matching algorithms require controlled software release (not config-only)",
            "Execution at passive order's price - aggressive order gets price improvement",
            "STP: cancel-newest (FR-1 default); all modes in Enterprise",
        ],
        "Existing priority models are configuration-selectable. Materially new logic needs a code release.",
        "")

    # --- Slide 11: Journaling and Durability ---
    add_content_slide(prs,
        "Journal, Snapshots, and Durability Modes",
        [
            "Journal = single source of truth. Append-only, checksummed, locally durable.",
            "Regulatory-Grade (default): fsync before processing. Zero data loss.",
            "Reduced-Latency (opt-in): batched writes. Data-loss window = batch interval.",
            "FR-1 baseline: local journal + snapshot/replay + warm restart (single site)",
            "DR journal replication: Phase 2+ unless explicitly included in FR-1 scope",
        ],
        "FR-1 ships local durable journal + warm restart. DR replication is Phase 2. [OPEN: D-06 journal implementation]",
        "Diagram: 10-journaling-replay-recovery.drawio (full)")

    # --- Slide 12: Recovery Model ---
    add_content_slide(prs,
        "Recovery - Warm Restart from Journal",
        [
            "Failure detected by watchdog (< 2 seconds)",
            "Load latest snapshot (all books + orders + seq# marker)",
            "Replay journal from snapshot point (deterministic - identical state)",
            "Validate recovered state (order count, checksums, seq# continuity)",
            "Resume processing (target: < 60 seconds for 100K resting orders)",
        ],
        "Determinism guarantee means recovery is exact, not approximate.",
        "Diagram: 10-journaling-replay-recovery.drawio (right panel)")

    # --- Slide 13: Gateway Architecture ---
    add_content_slide(prs,
        "Participant Connectivity - Gateways",
        [
            "FIX 4.4 gateway (FR-1): industry standard; broadest compatibility",
            "Binary gateway (SBE): lowest latency; for market makers and HFT",
            "Drop copy: real-time read-only trade/order feed for participants",
            "WS/REST gateway: Phase 2+ for lower-tier participants",
            "MM Quote gateway: Phase 2+ for mass-quote market makers",
        ],
        "FR-1 ships FIX + binary + drop copy. Covers critical participant base.",
        "Diagram: 05-participant-connectivity.drawio (full)")

    # --- Slide 14: Pre-Trade Risk ---
    add_content_slide(prs,
        "Pre-Trade Risk Controls",
        [
            "FR-1: fat finger (price/qty/notional), message rate throttle, kill switch",
            "Kill switch: cancel-all by participant/instrument/market-wide (< 1 second)",
            "Enterprise: adds credit limits, position limits, exposure limits, short-sell",
            "Risk checks execute BEFORE sequencer - not in engine hot path",
            "Limits configurable per participant, per instrument, in real-time",
        ],
        "Risk is on the ingress path but explicitly outside the deterministic engine hot path.",
        "")

    # --- Slide 15: Market Data ---
    add_content_slide(prs,
        "Market Data Dissemination",
        [
            "Full depth (all price levels) + trades + statistics",
            "Multicast (UDP) for colocated participants (FR-1)",
            "TCP/WebSocket for WAN distribution (Phase 2)",
            "Snapshot + incremental model with gap detection and recovery",
            "Published in exact engine processing order (sequenced)",
        ],
        "Completeness and correctness matter more than pure speed for most venues.",
        "")

    # --- Slide 16: Deployment Profiles ---
    add_table_slide(prs,
        "Three Deployment Profiles",
        ["", "Profile A", "Profile B", "Profile C"],
        [
            ["Use case", "Tier-1 exchange", "Most regulated venues", "Startup / carbon / crossing"],
            ["Hardware", "Bare metal, DPDK", "Bare metal, standard", "Cloud bare-metal / VM"],
            ["Clock", "PTP + GPS", "PTP", "NTP or PTP"],
            ["Engine p99", "< 5 us (target)", "< 20 us (target)", "< 100 us (target)"],
            ["Wire-to-wire p99", "< 25 us (target)", "< 100 us (target)", "< 1 ms (target)"],
            ["Fairness delta", "< 1 us (HW) / < 5 us (SW)", "< 5 us", "< 50 us"],
        ],
        "Same platform binary. All figures are engineering targets validated through performance testing.",
        assumption_banner=False)

    # --- Slide 17: Performance Budget ---
    add_table_slide(prs,
        "End-to-End Latency Budget (Profile A)",
        ["Component", "Target (p99)", "Notes"],
        [
            ["NIC receive + GW process", "~3 us", "DPDK poll-mode"],
            ["Risk check", "~3 us", "Fat finger, throttle"],
            ["Sequencer + journal fsync", "~3 us", "Regulatory-Grade mode"],
            ["Engine match", "~5 us", "Single-threaded partition"],
            ["Output + fan-out", "~5 us", "Lock-free ring buffer"],
            ["TOTAL wire-to-wire", "~20 us", "Profile A engineering target"],
        ],
        "Largest contributor is engine match (~5 us). Journal fsync adds ~2-3 us in Reg-Grade mode.",
        assumption_banner=True)

    # --- Slide 18: Technology Options ---
    add_table_slide(prs,
        "Technology Stack - Open Decision",
        ["Component", "Recommendation", "Status"],
        [
            ["Hot-path language", "C++ (primary) or Rust (alternative)", "[OPEN D-01]"],
            ["Control plane", "Java/Kotlin or Go", "Recommendation"],
            ["Inter-service messaging", "Aeron (engine), Kafka (surveillance)", "Recommendation"],
            ["Monitoring", "Prometheus + Grafana", "Recommendation"],
            ["Journal", "Custom NVMe or Aeron Archive", "[OPEN D-06]"],
            ["Containers", "Kubernetes (non-hot-path only)", "Recommendation"],
            ["FPGA", "Not recommended for FR-1", "Recommendation"],
        ],
        "Open decisions marked. Recommendations are provisional until CTO confirms.",
        assumption_banner=False)

    # --- Slide 19: HA/DR ---
    add_content_slide(prs,
        "HA/DR - Active-Passive Reference Model",
        [
            "Primary site: active. DR site: passive/hot standby (journal replicated)",
            "Failover: detect (< 2s) > decide > promote (< 60s) > validate > activate",
            "Total RTO target: < 5 minutes. RPO: near-zero (replication lag).",
            "FR-1: warm restart only (single site). DR is Phase 2.",
            "Active-active is a Phase 4 consideration for 24/7 venues",
        ],
        "Reference model is active-passive. Operator authorization for failover is configurable.",
        "Diagram: 11-ha-dr-failover.drawio (full)")

    # --- Slide 20: Compliance ---
    add_content_slide(prs,
        "Compliance-by-Configuration",
        [
            "Jurisdiction-agnostic core with configurable policy profiles",
            "Profile defines: clock sync, transparency, risk controls, reporting, retention",
            "Example profiles: EU MiFID II, US Reg ATS, SG MAS, UAE VARA",
            "Audit trail: 100% state mutations, nanosecond timestamps, tamper-evident",
            "Surveillance: data export hooks (FR-1); pattern detection Phase 2 (build vs. buy open)",
        ],
        "Compliance layer is configuration overlay, not code branch. [OPEN: D-03 jurisdiction, D-08 surveillance]",
        "")

    # --- Slide 21: Component Interaction ---
    add_content_slide(prs,
        "Full Component Interaction Flow",
        [
            "8-step numbered flow: submit > validate > risk > sequence > journal > match > output > deliver",
            "Supporting services: reference data, operations console, kill switch",
            "WS/REST gateway marked Phase 2+",
            "All components connected through defined interfaces",
        ],
        "Detailed 'how it all fits together' view for engineering alignment.",
        "Diagram: 03-component-interaction.drawio (full)")

    # --- Slide 22: FR-1 Scope ---
    add_table_slide(prs,
        "Foundation Release (FR-1) - What Ships",
        ["Area", "FR-1 Scope", "Deferred to Enterprise"],
        [
            ["Matching", "Continuous LOB, price-time FIFO", "Pro-rata, RFQ, dark"],
            ["Order types", "Market, Limit, IOC, FOK, GTC, Day", "Stop, Iceberg, Pegged, Complex"],
            ["Connectivity", "FIX 4.4 + Binary + Drop Copy", "WS/REST, MM Quote GW"],
            ["Risk", "Fat finger, throttle, kill switch", "Credit, position, short-sell"],
            ["Post-trade", "Capture, fees, confirmation, EOD", "CCP clearing, allocations"],
            ["DR", "Warm restart (single site)", "Active-passive DR site"],
            ["Surveillance", "File-based event export", "Real-time stream, detection"],
        ],
        "FR-1 is production-capable for initial market scope. Enterprise features are additive.",
        assumption_banner=False)

    # --- Slide 23: Workstreams ---
    add_table_slide(prs,
        "14 Implementation Workstreams",
        ["#", "Workstream", "Key Roles", "Phase"],
        [
            ["WS-01", "Market Microstructure / Rules", "Product, Compliance", "Decide"],
            ["WS-02", "Core Matching Engine", "3-4 Sr Engineers", "Build Core"],
            ["WS-03", "Gateways / Connectivity", "2 Engineers", "Build Core"],
            ["WS-04", "Pre-Trade Risk", "1-2 Engineers", "Build Core"],
            ["WS-05", "Market Data", "1-2 Engineers", "Integrate"],
            ["WS-06", "Reference Data", "1 Engineer", "Build Core"],
            ["WS-07", "Journal / Recovery", "1-2 Engineers", "Build Core"],
            ["WS-08", "Post-Trade", "1-2 Engineers", "Integrate"],
            ["WS-09", "Surveillance / Compliance", "1 Engineer", "Integrate"],
            ["WS-10", "Infrastructure / Colo", "2-3 SRE", "Decide+Build"],
            ["WS-11", "Security / IAM", "1-2 Engineers", "Integrate"],
            ["WS-12", "Monitoring / SRE", "1-2 SRE", "Integrate"],
            ["WS-13", "Testing / Certification", "2-3 QA", "Certify"],
            ["WS-14", "Program / Rollout", "1 PM", "All phases"],
        ],
        "Net FR-1 headcount: 12-15 FTE (planning assumption). Several roles overlap across workstreams.",
        assumption_banner=True)

    # --- Slide 24: Milestones ---
    add_table_slide(prs,
        "Top 10 Milestones to FR-1",
        ["#", "Milestone", "Target", "Blocked By"],
        [
            ["M1", "Asset class + jurisdiction decided", "Month 0", "-"],
            ["M2", "Language decided", "Month 0", "-"],
            ["M6", "Single-instrument matching functional", "Month 3", "D-01"],
            ["M8", "FIX gateway functional", "Month 3", "-"],
            ["M11", "End-to-end order lifecycle", "Month 4", "M6, M8"],
            ["M15", "Cert environment available", "Month 6", "M10"],
            ["M16", "First participant certified", "Month 7", "M15"],
            ["M17", "Performance validated under load", "Month 7", "M10"],
            ["M19", "Dress rehearsal", "Month 8", "M18"],
            ["M20", "FR-1 GO-LIVE", "Month 9", "M19"],
        ],
        "All dates are planning targets from decision closure (Month 0).",
        assumption_banner=True)

    # --- Slide 25: Open Decisions ---
    add_table_slide(prs,
        "Open Design Decisions - Must Close Before Build",
        ["#", "Decision", "Owner", "Deadline", "Blocks FR-1?"],
        [
            ["D-01", "Hot-path language", "CTO", "Week 2", "YES"],
            ["D-02", "FR-1 asset class", "Product", "Week 1", "YES"],
            ["D-03", "Jurisdiction", "Compliance", "Week 1", "YES"],
            ["D-04", "Sequencing (HW/SW)", "Architecture", "Month 2", "YES"],
            ["D-05", "Colocation site", "Infrastructure", "Month 1", "YES"],
            ["D-06", "Journal implementation", "Eng Lead", "Month 2", "YES"],
        ],
        "Every week of decision delay is a week off the FR-1 timeline.",
        assumption_banner=False)

    # --- Slide 26: Delivery Risks ---
    add_table_slide(prs,
        "Top Delivery Risks",
        ["Risk", "Probability", "Impact", "Lead Mitigation"],
        [
            ["Decision closure delays", "High", "Critical", "Decision pack + deadlines + escalation"],
            ["Performance targets not met", "Medium", "High", "Early profiling from M3"],
            ["Hardware supply chain", "Medium", "High", "Order M1; cloud fallback"],
            ["Insufficient participants", "Medium", "Critical", "Incentive programs; partners"],
            ["Determinism bugs", "Low", "Critical", "Replay tests on every commit"],
            ["Scope creep", "High", "Medium", "Product owner empowered to defer"],
        ],
        "Decision closure delay is the #1 risk. Every risk has a named mitigation.",
        assumption_banner=False)

    # --- Slide 27: Build vs Buy ---
    add_table_slide(prs,
        "Build vs Buy Boundaries",
        ["Capability", "Recommendation", "Rationale"],
        [
            ["Matching engine core", "BUILD", "Core differentiator"],
            ["Gateways", "HYBRID (lib + custom)", "FIX engine library; custom binary"],
            ["Surveillance", "BUY", "Specialized vendor domain"],
            ["Monitoring", "BUY/OSS", "Prometheus, Grafana, ELK"],
            ["PTP infrastructure", "BUY", "Meinberg, Spectracom"],
            ["HSM", "BUY", "Thales, nCipher"],
        ],
        "Build the core differentiator. Buy everything else where mature solutions exist.",
        assumption_banner=False)

    # --- Slide 28: Rollout Plan ---
    add_content_slide(prs,
        "Rollout Plan - FR-1 to Production",
        [
            "Internal testing: platform running with synthetic load (4 weeks)",
            "Participant certification: selected participants certify connectivity (6 weeks)",
            "Market rehearsal: full dress rehearsal with all participants (2 weeks)",
            "Soft launch: limited instruments, limited participants, real trading (2 weeks)",
            "Full launch: all instruments, all participants, production operation",
        ],
        "GO/NO-GO decision gate between certification and soft launch.",
        "Diagram: 12-rollout-migration-swimlane.drawio (simplified)")

    # --- Slide 29: Next Steps ---
    add_content_slide(prs,
        "Next Steps",
        [
            "Week 1: Decision workshop for D-01 through D-06",
            "Week 2: Close decisions; begin hiring; order hardware",
            "Month 1: Engine skeleton + build pipeline operational",
            "Month 3: End-to-end order lifecycle functional",
            "Month 9: FR-1 go-live (planning target)",
        ],
        "Next concrete step: schedule the decision workshop.",
        "")

    # --- Slide 30: End ---
    add_title_slide(prs,
        "Architecture Baseline Accepted",
        "30 specification files  |  12 architecture diagrams  |  Frozen v1.0.0-FINAL\nReady for decision workshop and implementation planning",
        "")

    path = os.path.join(OUT_DIR, "Matching-Engine-Architecture-Review-v1.0.pptx")
    prs.save(path)
    print(f"Created: {path}")
    return path


# ═══════════════════════════════════════════════════════════════
if __name__ == "__main__":
    print("Generating decks...")
    p1 = create_executive_deck()
    p2 = create_architecture_deck()
    print(f"\nDone. Files:\n  {p1}\n  {p2}")
