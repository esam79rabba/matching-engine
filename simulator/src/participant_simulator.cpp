#include "simulator/participant_simulator.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <iostream>
#include <algorithm>

#ifdef OPENSSL_AVAILABLE
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

namespace simulator {

// SOH delimiter used in real FIX messages.
static constexpr char SOH = '\x01';

// ─── Construction / Destruction ──────────────────────────────────

ParticipantSimulator::ParticipantSimulator(const SimulatorConfig& config)
    : config_(config) {}

ParticipantSimulator::~ParticipantSimulator() {
    stop();
    disconnect();
}

// ─── Public interface ────────────────────────────────────────────

SimulatorStats ParticipantSimulator::run() {
    running_.store(true);

    // Step 1: Connect
    if (!connect()) {
        std::lock_guard<std::mutex> lk(stats_mutex_);
        stats_.errors++;
        running_.store(false);
        return stats_;
    }

    // Step 2: Logon
    std::string logon_msg = build_logon();
    if (!send_fix(logon_msg)) {
        std::lock_guard<std::mutex> lk(stats_mutex_);
        stats_.errors++;
        disconnect();
        running_.store(false);
        return stats_;
    }

    std::string logon_resp = recv_fix();
    if (logon_resp.empty() || extract_tag(logon_resp, 35) != "A") {
        std::lock_guard<std::mutex> lk(stats_mutex_);
        stats_.errors++;
        disconnect();
        running_.store(false);
        return stats_;
    }

    // Step 3: Trading loop
    trading_loop();

    // Step 4: Logout
    std::string logout_msg = build_logout();
    send_fix(logout_msg);
    // Try to read logout ack but don't block long
    recv_fix();

    disconnect();
    running_.store(false);

    std::lock_guard<std::mutex> lk(stats_mutex_);
    return stats_;
}

void ParticipantSimulator::stop() {
    running_.store(false);
}

SimulatorStats ParticipantSimulator::stats() const {
    std::lock_guard<std::mutex> lk(stats_mutex_);
    return stats_;
}

// ─── Connection ──────────────────────────────────────────────────

bool ParticipantSimulator::connect() {
    disconnect();
    msg_seq_ = 1;

    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) return false;

    // Set receive timeout (2 seconds)
    struct timeval tv{};
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.port);
    inet_pton(AF_INET, config_.host.c_str(), &addr.sin_addr);

    if (::connect(socket_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

#ifdef OPENSSL_AVAILABLE
    if (config_.use_tls) {
        SSL_library_init();
        SSL_load_error_strings();

        const SSL_METHOD* method = TLS_client_method();
        ssl_ctx_ = SSL_CTX_new(method);
        if (!ssl_ctx_) {
            ::close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }

        ssl_ = SSL_new(static_cast<SSL_CTX*>(ssl_ctx_));
        SSL_set_fd(static_cast<SSL*>(ssl_), socket_fd_);

        if (SSL_connect(static_cast<SSL*>(ssl_)) <= 0) {
            SSL_free(static_cast<SSL*>(ssl_));
            SSL_CTX_free(static_cast<SSL_CTX*>(ssl_ctx_));
            ssl_ = nullptr;
            ssl_ctx_ = nullptr;
            ::close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
    }
#endif

    return true;
}

void ParticipantSimulator::disconnect() {
#ifdef OPENSSL_AVAILABLE
    if (ssl_) {
        SSL_shutdown(static_cast<SSL*>(ssl_));
        SSL_free(static_cast<SSL*>(ssl_));
        ssl_ = nullptr;
    }
    if (ssl_ctx_) {
        SSL_CTX_free(static_cast<SSL_CTX*>(ssl_ctx_));
        ssl_ctx_ = nullptr;
    }
#endif

    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
}

bool ParticipantSimulator::send_fix(const std::string& msg) {
    if (socket_fd_ < 0) return false;

#ifdef OPENSSL_AVAILABLE
    if (ssl_) {
        int written = SSL_write(static_cast<SSL*>(ssl_), msg.c_str(),
                                static_cast<int>(msg.size()));
        return written == static_cast<int>(msg.size());
    }
#endif

    ssize_t sent = ::send(socket_fd_, msg.c_str(), msg.size(), 0);
    return sent == static_cast<ssize_t>(msg.size());
}

std::string ParticipantSimulator::recv_fix() {
    if (socket_fd_ < 0) return {};

    char buf[8192];

#ifdef OPENSSL_AVAILABLE
    if (ssl_) {
        int n = SSL_read(static_cast<SSL*>(ssl_), buf, sizeof(buf) - 1);
        if (n <= 0) return {};
        buf[n] = '\0';
        return std::string(buf, static_cast<size_t>(n));
    }
#endif

    ssize_t n = ::recv(socket_fd_, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return {};

    buf[n] = '\0';
    return std::string(buf, static_cast<size_t>(n));
}

// ─── FIX message construction ────────────────────────────────────

std::string ParticipantSimulator::calculate_checksum(const std::string& message) {
    uint32_t sum = 0;
    for (unsigned char c : message) {
        sum += c;
    }
    std::ostringstream oss;
    oss << std::setw(3) << std::setfill('0') << (sum % 256);
    return oss.str();
}

std::string ParticipantSimulator::build_fix_message(const std::string& body) const {
    std::string begin_str = std::string("8=FIX.4.4") + SOH;
    std::string body_length_tag = "9=" + std::to_string(body.size()) + SOH;
    std::string pre_checksum = begin_str + body_length_tag + body;
    std::string checksum = calculate_checksum(pre_checksum);
    return pre_checksum + "10=" + checksum + SOH;
}

std::string ParticipantSimulator::timestamp() const {
    return "20260327-12:00:00.000";
}

std::string ParticipantSimulator::next_cl_ord_id() {
    return "SIM" + std::to_string(cl_ord_id_++);
}

std::string ParticipantSimulator::build_logon() {
    std::ostringstream oss;
    oss << "35=A" << SOH
        << "49=" << config_.sender_comp_id << SOH
        << "56=" << config_.target_comp_id << SOH
        << "34=" << msg_seq_++ << SOH
        << "52=" << timestamp() << SOH
        << "98=0" << SOH
        << "108=" << config_.heartbeat_sec << SOH;
    return build_fix_message(oss.str());
}

std::string ParticipantSimulator::build_logout() {
    std::ostringstream oss;
    oss << "35=5" << SOH
        << "49=" << config_.sender_comp_id << SOH
        << "56=" << config_.target_comp_id << SOH
        << "34=" << msg_seq_++ << SOH
        << "52=" << timestamp() << SOH;
    return build_fix_message(oss.str());
}

std::string ParticipantSimulator::build_new_order(
    const std::string& cl_ord_id, const std::string& side,
    int64_t price, uint64_t qty) {

    std::ostringstream oss;
    oss << "35=D" << SOH
        << "49=" << config_.sender_comp_id << SOH
        << "56=" << config_.target_comp_id << SOH
        << "34=" << msg_seq_++ << SOH
        << "52=" << timestamp() << SOH
        << "11=" << cl_ord_id << SOH
        << "55=" << config_.symbol << SOH
        << "54=" << side << SOH
        << "40=2" << SOH    // OrdType = Limit
        << "44=" << price << SOH
        << "38=" << qty << SOH
        << "59=0" << SOH;   // TIF = Day
    return build_fix_message(oss.str());
}

std::string ParticipantSimulator::build_cancel(
    const std::string& cl_ord_id, const std::string& orig_order_id,
    const std::string& side) {

    std::ostringstream oss;
    oss << "35=F" << SOH
        << "49=" << config_.sender_comp_id << SOH
        << "56=" << config_.target_comp_id << SOH
        << "34=" << msg_seq_++ << SOH
        << "52=" << timestamp() << SOH
        << "11=" << cl_ord_id << SOH
        << "41=" << orig_order_id << SOH
        << "55=" << config_.symbol << SOH
        << "54=" << side << SOH;
    return build_fix_message(oss.str());
}

std::string ParticipantSimulator::build_heartbeat() {
    std::ostringstream oss;
    oss << "35=0" << SOH
        << "49=" << config_.sender_comp_id << SOH
        << "56=" << config_.target_comp_id << SOH
        << "34=" << msg_seq_++ << SOH
        << "52=" << timestamp() << SOH;
    return build_fix_message(oss.str());
}

// ─── FIX response parsing ────────────────────────────────────────

std::string ParticipantSimulator::extract_tag(const std::string& message, int tag) {
    std::string tag_prefix = std::to_string(tag) + "=";

    size_t pos = 0;
    while (pos < message.size()) {
        size_t found = message.find(tag_prefix, pos);
        if (found == std::string::npos) return {};

        // Verify at message start or preceded by SOH/pipe
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

// ─── Trading loop ────────────────────────────────────────────────

void ParticipantSimulator::trading_loop() {
    auto start = std::chrono::steady_clock::now();
    auto deadline = start + std::chrono::seconds(config_.duration_seconds);
    int cycle = 0;

    while (running_.load() && std::chrono::steady_clock::now() < deadline) {
        // Submit buy at base_price - spread
        {
            auto cid = next_cl_ord_id();
            int64_t buy_price = config_.base_price - config_.spread;
            std::string order = build_new_order(cid, "1", buy_price, config_.order_qty);
            if (send_fix(order)) {
                {
                    std::lock_guard<std::mutex> lk(stats_mutex_);
                    stats_.orders_sent++;
                }

                std::string resp = recv_fix();
                if (!resp.empty()) {
                    process_response(resp);
                    // Track for cancel
                    std::string venue_id = extract_tag(resp, 37);
                    if (!venue_id.empty()) {
                        outstanding_.push_back({cid, venue_id, "1"});
                    }
                }
            } else {
                std::lock_guard<std::mutex> lk(stats_mutex_);
                stats_.errors++;
            }
        }

        // Submit sell at base_price + spread
        {
            auto cid = next_cl_ord_id();
            int64_t sell_price = config_.base_price + config_.spread;
            std::string order = build_new_order(cid, "2", sell_price, config_.order_qty);
            if (send_fix(order)) {
                {
                    std::lock_guard<std::mutex> lk(stats_mutex_);
                    stats_.orders_sent++;
                }

                std::string resp = recv_fix();
                if (!resp.empty()) {
                    process_response(resp);
                    std::string venue_id = extract_tag(resp, 37);
                    if (!venue_id.empty()) {
                        outstanding_.push_back({cid, venue_id, "2"});
                    }
                }
            } else {
                std::lock_guard<std::mutex> lk(stats_mutex_);
                stats_.errors++;
            }
        }

        // Periodically cancel stale orders and re-quote
        if (config_.cancel_stale && cycle % 5 == 4 && !outstanding_.empty()) {
            // Cancel oldest buy
            auto it = std::find_if(outstanding_.begin(), outstanding_.end(),
                [](const OutstandingOrder& o) { return o.side == "1"; });
            if (it != outstanding_.end()) {
                auto cxl_id = next_cl_ord_id();
                std::string cancel = build_cancel(cxl_id, it->venue_order_id, it->side);
                if (send_fix(cancel)) {
                    {
                        std::lock_guard<std::mutex> lk(stats_mutex_);
                        stats_.cancels_sent++;
                    }
                    std::string resp = recv_fix();
                    if (!resp.empty()) {
                        process_response(resp);
                    }
                }
                outstanding_.erase(it);
            }

            // Cancel oldest sell
            it = std::find_if(outstanding_.begin(), outstanding_.end(),
                [](const OutstandingOrder& o) { return o.side == "2"; });
            if (it != outstanding_.end()) {
                auto cxl_id = next_cl_ord_id();
                std::string cancel = build_cancel(cxl_id, it->venue_order_id, it->side);
                if (send_fix(cancel)) {
                    {
                        std::lock_guard<std::mutex> lk(stats_mutex_);
                        stats_.cancels_sent++;
                    }
                    std::string resp = recv_fix();
                    if (!resp.empty()) {
                        process_response(resp);
                    }
                }
                outstanding_.erase(it);
            }
        }

        cycle++;

        // Rate limit: sleep to maintain orders_per_second
        if (config_.orders_per_second > 0) {
            auto sleep_ms = std::chrono::milliseconds(1000 / config_.orders_per_second);
            std::this_thread::sleep_for(sleep_ms);
        }
    }
}

void ParticipantSimulator::process_response(const std::string& response) {
    std::string exec_type = extract_tag(response, 150);
    std::string msg_type = extract_tag(response, 35);

    std::lock_guard<std::mutex> lk(stats_mutex_);

    if (msg_type == "8") {
        // ExecutionReport
        if (exec_type == "0") {
            // New — order accepted
            stats_.acks_received++;
        } else if (exec_type == "F" || exec_type == "2") {
            // Trade / Filled
            stats_.fills_received++;
            std::string last_qty_str = extract_tag(response, 32);
            if (!last_qty_str.empty()) {
                stats_.total_volume += static_cast<uint64_t>(std::stod(last_qty_str));
            }
        } else if (exec_type == "8") {
            // Rejected
            stats_.rejects_received++;
        } else if (exec_type == "4") {
            // Canceled
            stats_.cancel_acks++;
        }
    }

    if (exec_cb_) {
        exec_cb_(exec_type, response);
    }
}

}  // namespace simulator
