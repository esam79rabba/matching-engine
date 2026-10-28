#include "certification/cert_harness.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <cctype>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace certification {

// SOH delimiter used in real FIX messages.
static constexpr char SOH = '\x01';

// ─── Construction / Destruction ──────────────────────────────────

CertificationHarness::CertificationHarness(const CertConfig& config)
    : config_(config) {}

CertificationHarness::~CertificationHarness() {
    disconnect();
}

// ─── Socket management ──────────────────────────────────────────

bool CertificationHarness::connect_to_server() {
    disconnect();
    // Reset sequence for new connection — each TCP connection creates
    // a fresh FIX session on the server with expected inbound_seq=1
    msg_seq_ = 1;
    rx_buf_.clear();

    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) return false;

    // Set receive timeout
    struct timeval tv{};
    tv.tv_sec  = config_.recv_timeout_ms / 1000;
    tv.tv_usec = (config_.recv_timeout_ms % 1000) * 1000;
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(config_.port);
    inet_pton(AF_INET, config_.host.c_str(), &addr.sin_addr);

    if (::connect(socket_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    return true;
}

void CertificationHarness::disconnect() {
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
    rx_buf_.clear();
}

bool CertificationHarness::ensure_connected() {
    if (socket_fd_ >= 0) return true;
    return connect_to_server();
}

// ─── FIX message construction ───────────────────────────────────

std::string CertificationHarness::calculate_checksum(const std::string& message) {
    uint32_t sum = 0;
    for (unsigned char c : message) {
        sum += c;
    }
    std::ostringstream oss;
    oss << std::setw(3) << std::setfill('0') << (sum % 256);
    return oss.str();
}

std::string CertificationHarness::build_fix_message(const std::string& body) const {
    // BeginString and BodyLength are computed over the body.
    // Body starts after tag 9 value's SOH and ends before tag 10.
    std::string begin_str = std::string("8=FIX.4.4") + SOH;

    // BodyLength = length of everything after tag 9's SOH up to (but not including) tag 10
    std::string body_length_tag = "9=" + std::to_string(body.size()) + SOH;

    std::string pre_checksum = begin_str + body_length_tag + body;
    std::string checksum = calculate_checksum(pre_checksum);

    return pre_checksum + "10=" + checksum + SOH;
}

std::string CertificationHarness::build_logon_body() {
    std::ostringstream oss;
    oss << "35=A" << SOH
        << "49=" << config_.sender_comp_id << SOH
        << "56=" << config_.target_comp_id << SOH
        << "34=" << msg_seq_++ << SOH
        << "52=" << "20260325-12:00:00.000" << SOH
        << "98=0" << SOH   // EncryptMethod = None
        << "108=" << config_.heartbeat_sec << SOH;
    return oss.str();
}

std::string CertificationHarness::build_new_order_body(
    const std::string& cl_ord_id,
    const std::string& symbol,
    const std::string& side,
    const std::string& price,
    const std::string& quantity) {

    std::ostringstream oss;
    oss << "35=D" << SOH
        << "49=" << config_.sender_comp_id << SOH
        << "56=" << config_.target_comp_id << SOH
        << "34=" << msg_seq_++ << SOH
        << "52=" << "20260325-12:00:00.000" << SOH
        << "11=" << cl_ord_id << SOH
        << "55=" << symbol << SOH
        << "54=" << side << SOH
        << "40=2" << SOH   // OrdType = Limit
        << "44=" << price << SOH
        << "38=" << quantity << SOH
        << "59=0" << SOH;  // TIF = Day
    return oss.str();
}

std::string CertificationHarness::build_cancel_body(
    const std::string& cl_ord_id,
    const std::string& orig_cl_ord_id,
    const std::string& symbol,
    const std::string& side) {

    std::ostringstream oss;
    oss << "35=F" << SOH
        << "49=" << config_.sender_comp_id << SOH
        << "56=" << config_.target_comp_id << SOH
        << "34=" << msg_seq_++ << SOH
        << "52=" << "20260325-12:00:00.000" << SOH
        << "11=" << cl_ord_id << SOH
        << "41=" << orig_cl_ord_id << SOH
        << "55=" << symbol << SOH
        << "54=" << side << SOH;
    return oss.str();
}

std::string CertificationHarness::next_cl_ord_id() {
    return std::to_string(cl_ord_id_++);
}

// ─── FIX send / receive ─────────────────────────────────────────

bool CertificationHarness::send_fix(const std::string& message) {
    if (socket_fd_ < 0) return false;
    ssize_t sent = ::send(socket_fd_, message.c_str(), message.size(), 0);
    return sent == static_cast<ssize_t>(message.size());
}

namespace {

/// Length of the first complete message in `buf` (ending after the
/// "<SOH>10=XXX<SOH>" trailer), or 0 if none is complete yet.
size_t complete_message_length(const std::string& buf) {
    size_t pos = 0;
    while ((pos = buf.find("10=", pos)) != std::string::npos) {
        if (pos > 0 && buf[pos - 1] == SOH && pos + 7 <= buf.size() &&
            std::isdigit(static_cast<unsigned char>(buf[pos + 3])) &&
            std::isdigit(static_cast<unsigned char>(buf[pos + 4])) &&
            std::isdigit(static_cast<unsigned char>(buf[pos + 5])) &&
            buf[pos + 6] == SOH) {
            return pos + 7;
        }
        ++pos;
    }
    return 0;
}

}  // namespace

std::string CertificationHarness::recv_fix() {
    if (socket_fd_ < 0) return {};

    // TCP is a byte stream: accumulate until one full message is buffered.
    for (;;) {
        if (size_t len = complete_message_length(rx_buf_)) {
            std::string msg = rx_buf_.substr(0, len);
            rx_buf_.erase(0, len);
            return msg;
        }
        char buf[4096];
        ssize_t n = ::recv(socket_fd_, buf, sizeof(buf), 0);
        if (n <= 0) return {};  // timeout, error, or peer closed
        rx_buf_.append(buf, static_cast<size_t>(n));
    }
}

bool CertificationHarness::establish_session(std::string& error) {
    if (!connect_to_server()) {
        error = "Failed to connect";
        return false;
    }
    if (!send_fix(build_fix_message(build_logon_body()))) {
        disconnect();
        error = "Failed to send Logon";
        return false;
    }
    std::string ack = recv_fix();
    if (ack.empty()) {
        disconnect();
        error = "No Logon ack received";
        return false;
    }
    if (extract_tag(ack, 35) != "A") {
        error = "Expected Logon ack 35=A, got 35=" + extract_tag(ack, 35);
        disconnect();
        return false;
    }
    return true;
}

// ─── FIX response parsing ───────────────────────────────────────

std::string CertificationHarness::extract_tag(const std::string& message, int tag) {
    std::string tag_prefix = std::to_string(tag) + "=";

    // Search for tag after SOH or at start of message
    size_t pos = 0;
    while (pos < message.size()) {
        size_t found = message.find(tag_prefix, pos);
        if (found == std::string::npos) return {};

        // Verify it's at message start or preceded by SOH
        if (found == 0 || message[found - 1] == SOH || message[found - 1] == '|') {
            size_t val_start = found + tag_prefix.size();
            size_t val_end = message.find_first_of(std::string(1, SOH) + "|", val_start);
            if (val_end == std::string::npos) val_end = message.size();
            return message.substr(val_start, val_end - val_start);
        }
        pos = found + 1;
    }
    return {};
}

bool CertificationHarness::is_exec_report_with_type(
    const std::string& message, const std::string& exec_type) {
    return extract_tag(message, 35) == "8" && extract_tag(message, 150) == exec_type;
}

// ─── Certification scenarios ────────────────────────────────────
//
// Design note: every scenario except Logon runs on ONE connection:
// Logon -> read 35=A -> application message(s) -> disconnect. The server
// frames the TCP stream, so multi-step scenarios (cancel, crossing trade)
// reuse the same session and sequence numbers.

CertResult CertificationHarness::run_logon_test() {
    const std::string scenario = "Logon";

    if (!connect_to_server()) {
        return {false, scenario, "Failed to connect to server"};
    }

    std::string logon = build_fix_message(build_logon_body());
    if (!send_fix(logon)) {
        disconnect();
        return {false, scenario, "Failed to send Logon message"};
    }

    std::string response = recv_fix();
    disconnect();

    if (response.empty()) {
        return {false, scenario, "No response received from server"};
    }

    std::string msg_type = extract_tag(response, 35);
    if (msg_type == "A") {
        return {true, scenario, "Logon acknowledged (35=A received)"};
    }

    return {false, scenario, "Expected 35=A, got 35=" + msg_type};
}

CertResult CertificationHarness::run_valid_order_test() {
    const std::string scenario = "ValidOrder";

    // Logon, then NewOrderSingle on the same connection
    std::string err;
    if (!establish_session(err)) return {false, scenario, err};

    auto ord_id = next_cl_ord_id();
    std::string order = build_fix_message(
        build_new_order_body(ord_id, "BTC-USD", "1", "49000", "5"));
    if (!send_fix(order)) {
        disconnect();
        return {false, scenario, "Failed to send NewOrderSingle"};
    }

    std::string response = recv_fix();
    disconnect();

    if (response.empty()) {
        return {false, scenario, "No response to NewOrderSingle"};
    }

    // Expect ExecutionReport (35=8) with ExecType=New (150=0)
    if (is_exec_report_with_type(response, "0")) {
        return {true, scenario, "Order accepted (35=8, 150=0)"};
    }

    std::string mt = extract_tag(response, 35);
    std::string et = extract_tag(response, 150);
    return {false, scenario,
            "Expected 35=8/150=0, got 35=" + mt + "/150=" + et};
}

CertResult CertificationHarness::run_rejected_order_test() {
    const std::string scenario = "RejectedOrder";

    // Logon, then an order with an invalid instrument on the same connection
    std::string err;
    if (!establish_session(err)) return {false, scenario, err};

    auto ord_id = next_cl_ord_id();
    std::string order = build_fix_message(
        build_new_order_body(ord_id, "INVALID-INST", "1", "100", "1"));
    if (!send_fix(order)) {
        disconnect();
        return {false, scenario, "Failed to send NewOrderSingle"};
    }

    std::string response = recv_fix();
    disconnect();

    if (response.empty()) {
        return {false, scenario, "No response to invalid order"};
    }

    // Accept either ExecutionReport/Rejected (35=8/150=8) or BusinessReject (35=j)
    std::string msg_type = extract_tag(response, 35);
    if (msg_type == "j") {
        return {true, scenario, "Invalid order rejected (35=j BusinessReject)"};
    }
    if (is_exec_report_with_type(response, "8")) {
        return {true, scenario, "Invalid order rejected (35=8, 150=8)"};
    }

    std::string et = extract_tag(response, 150);
    return {false, scenario,
            "Expected rejection (35=j or 35=8/150=8), got 35=" + msg_type + "/150=" + et};
}

CertResult CertificationHarness::run_cancel_test() {
    const std::string scenario = "CancelOrder";

    // Step 1: Logon, then submit a resting order
    std::string err;
    if (!establish_session(err)) return {false, scenario, err};

    auto orig_id = next_cl_ord_id();
    std::string order = build_fix_message(
        build_new_order_body(orig_id, "BTC-USD", "1", "45000", "10"));
    if (!send_fix(order)) {
        disconnect();
        return {false, scenario, "Failed to send order"};
    }

    std::string order_resp = recv_fix();

    if (!is_exec_report_with_type(order_resp, "0")) {
        disconnect();
        return {false, scenario,
                "Order not accepted before cancel (got 35=" +
                extract_tag(order_resp, 35) + "/150=" +
                extract_tag(order_resp, 150) + ")"};
    }

    // Extract venue order ID from the ack (tag 37) for the cancel request
    std::string venue_order_id = extract_tag(order_resp, 37);

    // Step 2: Send cancel request on the same session
    auto cxl_id = next_cl_ord_id();
    // Use venue_order_id as OrigClOrdID (tag 41) if available, else use client order id
    std::string orig_ref = venue_order_id.empty() ? orig_id : venue_order_id;
    std::string cancel = build_fix_message(
        build_cancel_body(cxl_id, orig_ref, "BTC-USD", "1"));
    if (!send_fix(cancel)) {
        disconnect();
        return {false, scenario, "Failed to send CancelRequest"};
    }

    std::string cancel_resp = recv_fix();
    disconnect();

    if (cancel_resp.empty()) {
        return {false, scenario, "No response to CancelRequest"};
    }

    // Expect ExecutionReport with ExecType=Canceled (150=4)
    if (is_exec_report_with_type(cancel_resp, "4")) {
        return {true, scenario, "Order cancelled (35=8, 150=4)"};
    }

    std::string mt = extract_tag(cancel_resp, 35);
    std::string et = extract_tag(cancel_resp, 150);
    return {false, scenario,
            "Expected 35=8/150=4, got 35=" + mt + "/150=" + et};
}

CertResult CertificationHarness::run_crossing_trade_test() {
    const std::string scenario = "CrossingTrade";

    // Step 1: Logon, then submit resting sell
    std::string err;
    if (!establish_session(err)) return {false, scenario, err};

    auto sell_id = next_cl_ord_id();
    std::string sell = build_fix_message(
        build_new_order_body(sell_id, "BTC-USD", "2", "50000", "10"));
    if (!send_fix(sell)) {
        disconnect();
        return {false, scenario, "Failed to send sell order"};
    }

    std::string sell_resp = recv_fix();

    if (!is_exec_report_with_type(sell_resp, "0")) {
        disconnect();
        return {false, scenario,
                "Sell order not accepted (got 35=" +
                extract_tag(sell_resp, 35) + "/150=" +
                extract_tag(sell_resp, 150) + ")"};
    }

    // Step 2: Submit crossing buy at same price on the same session
    auto buy_id = next_cl_ord_id();
    std::string buy = build_fix_message(
        build_new_order_body(buy_id, "BTC-USD", "1", "50000", "10"));
    if (!send_fix(buy)) {
        disconnect();
        return {false, scenario, "Failed to send buy order"};
    }

    std::string buy_resp = recv_fix();
    disconnect();

    if (buy_resp.empty()) {
        return {false, scenario, "No response to crossing buy"};
    }

    // Expect a Trade execution report (150=F) somewhere in response.
    if (is_exec_report_with_type(buy_resp, "F")) {
        return {true, scenario, "Trade executed (35=8, 150=F)"};
    }

    // Also accept 150=2 (Filled) which some implementations use
    if (is_exec_report_with_type(buy_resp, "2")) {
        return {true, scenario, "Trade executed (35=8, 150=2 Filled)"};
    }

    std::string mt = extract_tag(buy_resp, 35);
    std::string et = extract_tag(buy_resp, 150);
    return {false, scenario,
            "Expected 35=8/150=F (Trade), got 35=" + mt + "/150=" + et};
}

CertResult CertificationHarness::run_halted_instrument_test() {
    const std::string scenario = "HaltedInstrument";

    // Logon, then an order for a halted instrument on the same connection.
    // (The test fixture must halt the instrument before calling this scenario.)
    std::string err;
    if (!establish_session(err)) return {false, scenario, err};

    auto ord_id = next_cl_ord_id();
    std::string order = build_fix_message(
        build_new_order_body(ord_id, "BTC-USD", "1", "50000", "5"));
    if (!send_fix(order)) {
        disconnect();
        return {false, scenario, "Failed to send order"};
    }

    std::string response = recv_fix();
    disconnect();

    if (response.empty()) {
        return {false, scenario, "No response for halted instrument order"};
    }

    // Expect rejection: 35=8/150=8 (Rejected) or 35=j
    std::string msg_type = extract_tag(response, 35);
    if (msg_type == "j") {
        return {true, scenario, "Order rejected for halted instrument (35=j)"};
    }
    if (is_exec_report_with_type(response, "8")) {
        return {true, scenario, "Order rejected for halted instrument (35=8, 150=8)"};
    }

    std::string et = extract_tag(response, 150);
    return {false, scenario,
            "Expected rejection, got 35=" + msg_type + "/150=" + et};
}

// ─── Run all scenarios ──────────────────────────────────────────

CertificationReport CertificationHarness::run_all() {
    CertificationReport report;

    report.record(run_logon_test());
    report.record(run_valid_order_test());
    report.record(run_rejected_order_test());
    report.record(run_cancel_test());
    report.record(run_crossing_trade_test());
    report.record(run_halted_instrument_test());

    return report;
}

}  // namespace certification
