#pragma once

/// Client Certification Harness — Milestone 11 Artifact 1.
///
/// Proves external integration readiness over the real FIX TCP transport.
/// The harness acts as an EXTERNAL client: it connects via raw POSIX sockets
/// and speaks FIX 4.4 wire protocol only. No engine internals are used.
///
/// Each scenario method returns a CertResult indicating pass/fail with
/// human-readable detail. run_all() executes the full certification suite
/// and produces a CertificationReport.

#include <cstdint>
#include <string>
#include <vector>

namespace certification {

/// Result of a single certification scenario.
struct CertResult {
    bool        passed;
    std::string scenario;
    std::string details;
};

/// Aggregate report from a full certification run.
struct CertificationReport {
    uint32_t                total   = 0;
    uint32_t                passed  = 0;
    uint32_t                failed  = 0;
    std::vector<CertResult> details;

    void record(const CertResult& r) {
        details.push_back(r);
        ++total;
        if (r.passed) ++passed; else ++failed;
    }
};

/// Configuration for the certification harness connection.
struct CertConfig {
    std::string host          = "127.0.0.1";
    uint16_t    port          = 0;       // Must be set before use
    std::string sender_comp_id = "CERT_CLIENT";
    std::string target_comp_id = "EXCHANGE";
    uint32_t    heartbeat_sec  = 30;
    int         recv_timeout_ms = 2000;  // Socket receive timeout
};

/// Client certification harness.
///
/// Connects to a FIX TCP acceptor via raw POSIX sockets and exercises
/// the full order lifecycle through FIX 4.4 messages.
///
/// Usage:
///   CertConfig cfg;
///   cfg.port = 19876;
///   CertificationHarness harness(cfg);
///   auto report = harness.run_all();
///
/// The harness is instrument-agnostic. Instrument symbols and prices
/// are provided through scenario parameters or defaults.
class CertificationHarness {
public:
    explicit CertificationHarness(const CertConfig& config);
    ~CertificationHarness();

    // Non-copyable, non-movable (owns socket)
    CertificationHarness(const CertificationHarness&) = delete;
    CertificationHarness& operator=(const CertificationHarness&) = delete;

    // ─── Individual certification scenarios ──────────────────

    /// Logon: send 35=A, expect 35=A back.
    CertResult run_logon_test();

    /// Valid order: send 35=D with valid instrument, expect 35=8 with 150=0 (New).
    CertResult run_valid_order_test();

    /// Rejected order: send 35=D with invalid instrument, expect 35=8/150=8 or 35=j.
    CertResult run_rejected_order_test();

    /// Cancel: submit order then 35=F, expect 35=8 with 150=4 (Canceled).
    CertResult run_cancel_test();

    /// Crossing trade: resting sell + crossing buy, expect 35=8/150=F on both.
    CertResult run_crossing_trade_test();

    /// Halted instrument: expect reject when instrument is halted.
    CertResult run_halted_instrument_test();

    /// Run all certification scenarios and return aggregate report.
    CertificationReport run_all();

private:
    CertConfig  config_;
    int         socket_fd_ = -1;
    uint64_t    msg_seq_    = 1;
    uint64_t    cl_ord_id_  = 9000;

    // ─── Socket management ───────────────────────────────────

    bool connect_to_server();
    void disconnect();
    bool ensure_connected();

    // ─── FIX message construction ────────────────────────────

    /// Build a complete FIX message with BeginString, BodyLength, and Checksum.
    std::string build_fix_message(const std::string& body) const;

    /// Build Logon message body (without 8=/9=/10=).
    std::string build_logon_body();

    /// Build NewOrderSingle body.
    std::string build_new_order_body(const std::string& cl_ord_id,
                                      const std::string& symbol,
                                      const std::string& side,
                                      const std::string& price,
                                      const std::string& quantity);

    /// Build OrderCancelRequest body.
    std::string build_cancel_body(const std::string& cl_ord_id,
                                   const std::string& orig_cl_ord_id,
                                   const std::string& symbol,
                                   const std::string& side);

    /// Calculate FIX checksum (sum of bytes mod 256, zero-padded to 3 digits).
    static std::string calculate_checksum(const std::string& message);

    /// Get next unique ClOrdID.
    std::string next_cl_ord_id();

    // ─── FIX send / receive ──────────────────────────────────

    bool send_fix(const std::string& message);
    std::string recv_fix();

    // ─── FIX response parsing ────────────────────────────────

    /// Extract a tag value from a raw FIX message.
    static std::string extract_tag(const std::string& message, int tag);

    /// Check if response is an ExecutionReport (35=8) with given ExecType.
    static bool is_exec_report_with_type(const std::string& message, const std::string& exec_type);
};

}  // namespace certification
