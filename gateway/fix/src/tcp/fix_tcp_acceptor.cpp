#include "gateway/tcp/fix_tcp_acceptor.h"
#include "gateway/fix_framer.h"
#include "transport/tcp_socket.h"

#ifdef OPENSSL_AVAILABLE
#include "transport/tls_socket.h"
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>

namespace gateway {

FixTcpAcceptor::FixTcpAcceptor(uint16_t port) : port_(port) {}

FixTcpAcceptor::~FixTcpAcceptor() {
    stop();
}

void FixTcpAcceptor::set_server_socket(transport::IServerSocket* server) {
    server_socket_ = server;
    owns_server_socket_ = false;
}

void FixTcpAcceptor::set_tls_config(const transport::TlsConfig& config) {
    tls_config_ = config;
    has_tls_config_ = true;
}

void FixTcpAcceptor::set_session_store(FixSessionStore* store) {
    session_store_ = store;
}

void FixTcpAcceptor::set_message_store(FixMessageStore* store) {
    message_store_ = store;
}

bool FixTcpAcceptor::start() {
    // Create server socket if not injected
    if (!server_socket_) {
#ifdef OPENSSL_AVAILABLE
        if (has_tls_config_) {
            auto* tls_server = new transport::TlsServerSocket(port_, tls_config_);
            if (!tls_server->init_context()) {
                delete tls_server;
                return false;
            }
            server_socket_ = tls_server;
            owns_server_socket_ = true;
        } else
#endif
        {
            server_socket_ = new transport::TcpServerSocket(port_);
            owns_server_socket_ = true;
        }
    }

    // Only call listen() if the server socket isn't already listening
    // (injected sockets may already be in listening state)
    if (!server_socket_->is_listening()) {
        if (!server_socket_->listen()) {
            if (owns_server_socket_) {
                delete server_socket_;
                server_socket_ = nullptr;
                owns_server_socket_ = false;
            }
            return false;
        }
    }

    running_ = true;

    // Multi-threaded accept loop: each client handled in its own thread.
    // Production: epoll/kqueue with non-blocking I/O.
    while (running_) {
        int listen_fd = server_socket_->fd();
        if (listen_fd < 0) break;

        // Use a short timeout to check running_ flag
        struct timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);

        int ready = select(listen_fd + 1, &read_fds, nullptr, nullptr, &tv);
        if (ready <= 0) continue;
        if (!running_) break;

        transport::ISocket* client = server_socket_->accept();
        if (!client) continue;

        // Track ownership (protected by sockets_mutex_ for thread safety)
        {
            std::lock_guard<std::mutex> lock(sockets_mutex_);
            owned_sockets_.push_back(client);
        }

        // Handle client in a new thread — accept loop continues accepting
        active_clients_.fetch_add(1, std::memory_order_relaxed);
        std::thread([this, client]() {
            handle_client(client);
            active_clients_.fetch_sub(1, std::memory_order_relaxed);
        }).detach();
    }

    // Persist all active sessions if store is available
    if (session_store_) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (const auto& [id, session] : sessions_) {
            if (session.logged_in) {
                persist_session(session);
            }
        }
        session_store_->save();
    }

    // Close all client sockets
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (auto& [id, session] : sessions_) {
            if (session.socket) {
                session.socket->close();
                session.socket = nullptr;
            }
        }
        sessions_.clear();
    }

    // Delete all owned client sockets
    for (auto* sock : owned_sockets_) {
        delete sock;
    }
    owned_sockets_.clear();

    // Close and optionally delete server socket
    if (server_socket_) {
        server_socket_->close();
        if (owns_server_socket_) {
            delete server_socket_;
        }
        server_socket_ = nullptr;
        owns_server_socket_ = false;
    }

    return true;
}

void FixTcpAcceptor::stop() {
    running_ = false;

    // Wait for active client threads to finish (with timeout)
    int wait_ms = 0;
    while (active_clients_.load(std::memory_order_relaxed) > 0 && wait_ms < 5000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        wait_ms += 50;
    }
}

void FixTcpAcceptor::handle_client(transport::ISocket* client_socket) {
    // TCP is a byte stream: one recv() may return a partial FIX message,
    // several concatenated messages, or a message split across reads. All
    // bytes go through a per-connection framer that yields exactly one
    // complete message at a time.
    FixStreamFramer framer;
    char buf[8192];

    engine::SessionId sid = 0;
    bool have_session = false;

    auto notify_disconnect = [&]() {
        if (!have_session || !callbacks_.on_disconnect) return;
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(sid);
        if (it != sessions_.end() && it->second.logged_in) {
            callbacks_.on_disconnect(sid);
        }
    };

    while (running_.load()) {
        int client_fd = client_socket->fd();
        if (client_fd < 0) break;

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(client_fd, &read_fds);
        struct timeval tv{0, 100000};  // 100ms timeout to poll running_
        int ready = select(client_fd + 1, &read_fds, nullptr, nullptr, &tv);
        if (ready <= 0) continue;

        int nr = client_socket->recv(buf, sizeof(buf));
        if (nr <= 0) break;  // Client disconnected
        framer.append(buf, static_cast<size_t>(nr));

        // Drain every complete message currently buffered.
        std::string raw;
        bool done = false;
        while (!done) {
            const FrameStatus st = framer.next(raw);
            if (st == FrameStatus::NeedMore) break;
            if (st == FrameStatus::Error) {
                std::cerr << "[FIX] event=frame_error session="
                          << (have_session ? std::to_string(sid) : std::string("none"))
                          << " buffered=" << framer.buffered() << std::endl;
                client_socket->close();
                done = true;
                break;
            }

            // Bad checksum: drop this single message, keep the connection.
            if (!verify_checksum(raw)) {
                std::cerr << "[FIX] event=checksum_fail session="
                          << (have_session ? std::to_string(sid) : std::string("none"))
                          << std::endl;
                continue;
            }

            auto fields = FixParser::parse_fields(raw);
            auto msg_type = FixParser::get_msg_type(fields);
            if (!msg_type) {
                if (!have_session) {
                    client_socket->close();
                    done = true;
                }
                continue;
            }

            // Create the session on first valid contact.
            if (!have_session) {
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                sid = next_session_id_++;
                FixSession& session = sessions_[sid];
                session.session_id = sid;
                session.socket = client_socket;
                session.socket_fd = client_socket->fd();  // cached for backward compat
                have_session = true;
            }

            // Logon on a session that is not yet logged in.
            if (*msg_type == "A") {
                bool needs_logon = false;
                bool logon_ok = true;
                {
                    std::lock_guard<std::mutex> lock(sessions_mutex_);
                    auto it = sessions_.find(sid);
                    if (it != sessions_.end() && !it->second.logged_in) {
                        needs_logon = true;
                        logon_ok = process_logon(it->second, fields);
                        if (!logon_ok) {
                            client_socket->close();
                            sessions_.erase(sid);
                        }
                    }
                }
                if (needs_logon) {
                    if (!logon_ok) return;  // session erased; nothing to notify
                    continue;
                }
            }

            route_message(sid, *msg_type, raw, fields);

            // Exit on Logout.
            if (*msg_type == "5") done = true;
        }
        if (done) break;
    }

    notify_disconnect();
}

void FixTcpAcceptor::route_message(engine::SessionId sid, const std::string& msg_type,
                                     const std::string& raw,
                                     const std::unordered_map<int, std::string>& fields) {
    // Handle SequenceReset (35=4) — route before application logic
    if (msg_type == "4") {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(sid);
        if (it != sessions_.end()) {
            handle_sequence_reset(it->second, raw);
        }
        return;
    }

    // Handle ResendRequest (35=2)
    if (msg_type == "2") {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(sid);
        if (it != sessions_.end()) {
            handle_resend_request(it->second, raw);
        }
        return;
    }

    // Check inbound sequence for non-session-setup messages on logged-in sessions
    if (msg_type != "A") {
        auto seq_it = fields.find(34);
        if (seq_it != fields.end()) {
            uint64_t received_seq = std::stoull(seq_it->second);
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(sid);
            if (it != sessions_.end() && it->second.logged_in) {
                if (FixGapHandler::is_gap(received_seq, it->second.inbound_seq)) {
                    // Gap detected — send ResendRequest
                    auto resend = FixGapHandler::build_resend_request(
                        it->second.inbound_seq, received_seq - 1,
                        it->second.target_comp_id, it->second.sender_comp_id,
                        it->second.outbound_seq++);
                    send_to_socket(it->second, resend);
                    if (callbacks_.on_resend_request) {
                        callbacks_.on_resend_request(sid, resend);
                    }
                    return;  // Don't process — message is ahead of expected
                }
                it->second.inbound_seq = received_seq + 1;
            }
        }
    }

    // Route application messages
    if (msg_type != "A" && msg_type != "5" && msg_type != "0" && msg_type != "1") {
        // Per-message CompID validation (fail-closed): require a known,
        // logged-in session with tag 49 (SenderCompID) == session.sender_comp_id
        // and tag 56 (TargetCompID) == session.target_comp_id. Any failure
        // drops the session rather than routing to the engine.
        auto sender_it = fields.find(49);
        auto target_it = fields.find(56);
        bool deliver = false;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(sid);
            if (it == sessions_.end() || !it->second.logged_in) {
                std::cerr << "[FIX] event=app_msg_before_logon session=" << sid
                          << " msg_type=" << msg_type << "\n";
                if (it != sessions_.end()) handle_logout(it->second);
            } else {
                const bool sender_ok =
                    sender_it != fields.end() && sender_it->second == it->second.sender_comp_id;
                const bool target_ok =
                    target_it != fields.end() && target_it->second == it->second.target_comp_id;
                if (!sender_ok || !target_ok) {
                    std::cerr << "[FIX] event=comp_id_mismatch session=" << sid
                              << " expected_sender=" << it->second.sender_comp_id
                              << " got_sender=" << (sender_it != fields.end() ? sender_it->second : "<missing>")
                              << " expected_target=" << it->second.target_comp_id
                              << " got_target=" << (target_it != fields.end() ? target_it->second : "<missing>")
                              << "\n";
                    handle_logout(it->second);
                } else {
                    deliver = true;
                }
            }
        }
        if (deliver && callbacks_.on_message) {
            callbacks_.on_message(sid, raw);
        }
    }

    if (msg_type == "5") {  // Logout
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(sid);
        if (it != sessions_.end()) {
            handle_logout(it->second);
        }
    }

    if (msg_type == "0") {  // Heartbeat
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(sid);
        if (it != sessions_.end()) {
            handle_heartbeat(it->second);
        }
    }
}

bool FixTcpAcceptor::process_logon(FixSession& session,
                                     const std::unordered_map<int, std::string>& fields) {
    auto sender_it = fields.find(49);  // SenderCompID
    auto target_it = fields.find(56);  // TargetCompID
    auto hb_it = fields.find(108);     // HeartBtInt

    if (sender_it == fields.end() || target_it == fields.end()) return false;

    session.sender_comp_id = sender_it->second;
    session.target_comp_id = target_it->second;

    // Resolve participant from SenderCompID. Fail closed: no resolver or an
    // unknown CompID rejects the Logon; never fall back to a default id.
    std::optional<engine::ParticipantId> resolved;
    if (comp_id_resolver_) resolved = comp_id_resolver_(session.sender_comp_id);
    if (!resolved) {
        reject_logon(session, "Unknown SenderCompID");
        return false;
    }
    session.participant_id = *resolved;
    session.logged_in = true;

    if (hb_it != fields.end()) {
        session.heartbeat_interval_sec = static_cast<uint32_t>(std::stoul(hb_it->second));
    }

    // Extract participant's Logon MsgSeqNum
    auto seq_it = fields.find(34);
    uint64_t logon_seq = 0;
    bool has_logon_seq = false;
    if (seq_it != fields.end()) {
        logon_seq = std::stoull(seq_it->second);
        has_logon_seq = true;
    }

    // Restore persisted state if session store is available
    if (session_store_) {
        auto persisted = session_store_->get(session.sender_comp_id);
        if (persisted) {
            session.inbound_seq = persisted->inbound_seq;
            session.outbound_seq = persisted->outbound_seq;
        }
    }

    // Reconcile participant's Logon MsgSeqNum with expected inbound_seq.
    // After restart, the participant may have advanced beyond what we persisted.
    if (has_logon_seq) {
        if (logon_seq >= session.inbound_seq) {
            // Participant is at or ahead — accept and advance past the Logon
            session.inbound_seq = logon_seq + 1;
        }
        // If logon_seq < session.inbound_seq, participant is behind.
        // Accept logon anyway — they may reset or resend later.
    }

    // Send Logon ack
    uint64_t ack_seq = session.outbound_seq++;
    std::ostringstream ack;
    ack << "8=FIX.4.4\x01" << "35=A\x01"
        << "49=" << session.target_comp_id << "\x01"
        << "56=" << session.sender_comp_id << "\x01"
        << "34=" << ack_seq << "\x01"
        << "108=" << session.heartbeat_interval_sec << "\x01";
    std::string ack_str = ack.str();
    ack_str += "10=" + compute_fix_checksum(ack_str) + "\x01";

    // Store outbound logon ack for potential replay
    if (message_store_) {
        message_store_->store(session.target_comp_id, ack_seq, ack_str);
    }

    session.socket->send(ack_str.c_str(), ack_str.size());

    if (callbacks_.on_logon) {
        callbacks_.on_logon(session.session_id, session.participant_id);
    }

    return true;
}

void FixTcpAcceptor::reject_logon(FixSession& session, const std::string& text) {
    std::ostringstream out;
    out << "8=FIX.4.4\x01" << "35=5\x01"
        << "49=" << session.target_comp_id << "\x01"
        << "56=" << session.sender_comp_id << "\x01"
        << "34=1\x01"
        << "58=" << text << "\x01";
    std::string msg = out.str();
    msg += "10=" + compute_fix_checksum(msg) + "\x01";
    if (session.socket) session.socket->send(msg.c_str(), msg.size());
}

void FixTcpAcceptor::handle_heartbeat(FixSession& /*session*/) {
    // For prototype: acknowledge receipt. Production: track timing.
}

void FixTcpAcceptor::handle_logout(FixSession& session) {
    session.logged_in = false;

    // Persist session state before logout
    if (session_store_) {
        persist_session(session);
    }

    if (session.socket && session.socket->is_open()) {
        // Send Logout ack
        std::ostringstream msg;
        msg << "8=FIX.4.4\x01" << "35=5\x01"
            << "49=" << session.target_comp_id << "\x01"
            << "56=" << session.sender_comp_id << "\x01"
            << "34=" << session.outbound_seq++ << "\x01";
        std::string s = msg.str();
        s += "10=" + compute_fix_checksum(s) + "\x01";
        session.socket->send(s.c_str(), s.size());
        session.socket->close();
        session.socket = nullptr;
    }
    if (callbacks_.on_logout) {
        callbacks_.on_logout(session.session_id);
    }
}

void FixTcpAcceptor::close_session(engine::SessionId session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        if (it->second.socket) {
            it->second.socket->close();
            it->second.socket = nullptr;
        }
        if (callbacks_.on_disconnect) {
            callbacks_.on_disconnect(session_id);
        }
        sessions_.erase(it);
    }
}

bool FixTcpAcceptor::send(engine::SessionId session_id, const std::string& fix_message) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end() || !it->second.socket || !it->second.socket->is_open()) return false;

    // Store outbound message for potential replay
    if (message_store_ && !it->second.target_comp_id.empty()) {
        // Extract the sequence number from the message being sent
        auto fields = FixParser::parse_fields(fix_message);
        auto seq_it = fields.find(34);  // MsgSeqNum
        if (seq_it != fields.end()) {
            uint64_t seq = std::stoull(seq_it->second);
            message_store_->store(it->second.target_comp_id, seq, fix_message);
        }
    }

    int sent = it->second.socket->send(fix_message.c_str(), fix_message.size());
    return sent > 0;
}

uint32_t FixTcpAcceptor::session_count() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return static_cast<uint32_t>(sessions_.size());
}

engine::ParticipantId FixTcpAcceptor::participant_id_for_session(engine::SessionId sid) const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(sid);
    if (it == sessions_.end() || !it->second.logged_in) return 0;
    return it->second.participant_id;
}

std::string FixTcpAcceptor::sessions_json() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    std::string result = "[";
    bool first = true;
    for (const auto& [sid, session] : sessions_) {
        if (!first) result += ",";
        first = false;
        result += "{\"session_id\":" + std::to_string(session.session_id)
                + ",\"participant_id\":" + std::to_string(session.participant_id)
                + ",\"logged_in\":" + (session.logged_in ? "true" : "false")
                + ",\"sender_comp_id\":\"" + session.sender_comp_id + "\""
                + ",\"target_comp_id\":\"" + session.target_comp_id + "\""
                + ",\"inbound_seq\":" + std::to_string(session.inbound_seq)
                + ",\"outbound_seq\":" + std::to_string(session.outbound_seq)
                + "}";
    }
    result += "]";
    return result;
}

void FixTcpAcceptor::persist_session(const FixSession& session) {
    if (!session_store_) return;
    PersistedSessionState state;
    state.session_id = session.session_id;
    state.sender_comp_id = session.sender_comp_id;
    state.target_comp_id = session.target_comp_id;
    state.inbound_seq = session.inbound_seq;
    state.outbound_seq = session.outbound_seq;
    session_store_->upsert(state);
}

void FixTcpAcceptor::handle_sequence_reset(FixSession& session, const std::string& raw_fix) {
    auto info = FixGapHandler::parse_sequence_reset(raw_fix);
    if (!info) return;

    if (info->is_gap_fill) {
        // GapFill (123=Y): advance expected inbound_seq to new_seq_no
        session.inbound_seq = info->new_seq_no;
    } else {
        // Hard reset (123=N): set inbound_seq to new_seq_no
        session.inbound_seq = info->new_seq_no;
    }
}

void FixTcpAcceptor::handle_resend_request(FixSession& session, const std::string& raw_fix) {
    auto range = FixGapHandler::parse_resend_request(raw_fix);
    if (!range) return;

    uint64_t end = range->end_seq;
    if (end == 0) {
        end = session.outbound_seq > 0 ? session.outbound_seq - 1 : 0;
    }

    // If message store is available, replay stored messages
    if (message_store_) {
        auto stored = message_store_->get_range(session.target_comp_id,
                                                 range->begin_seq, end);

        // Build a set of found sequence numbers for gap detection
        std::unordered_map<uint64_t, std::string> found;
        for (auto& [seq, msg] : stored) {
            found[seq] = std::move(msg);
        }

        // Walk the range, replaying found messages and gap-filling gaps
        uint64_t gap_start = 0;
        bool in_gap = false;

        for (uint64_t seq = range->begin_seq; seq <= end; ++seq) {
            auto fit = found.find(seq);
            if (fit != found.end()) {
                // Close any open gap first
                if (in_gap) {
                    auto gf = FixGapHandler::build_gap_fill(
                        seq, gap_start,
                        session.target_comp_id, session.sender_comp_id);
                    send_to_socket(session, gf);
                    in_gap = false;
                }
                // Retransmit with PossDupFlag=Y (tag 43=Y)
                std::string replay_msg = add_poss_dup_flag(fit->second);
                send_to_socket(session, replay_msg);
            } else {
                // Start or continue a gap
                if (!in_gap) {
                    gap_start = seq;
                    in_gap = true;
                }
            }
        }

        // Close trailing gap if any
        if (in_gap) {
            auto gf = FixGapHandler::build_gap_fill(
                end + 1, gap_start,
                session.target_comp_id, session.sender_comp_id);
            send_to_socket(session, gf);
        }
    } else {
        // Fallback: no message store — send a single GapFill for the entire range
        auto gap_fill = FixGapHandler::build_gap_fill(
            end + 1, range->begin_seq,
            session.target_comp_id, session.sender_comp_id);
        send_to_socket(session, gap_fill);
    }
}

void FixTcpAcceptor::send_to_socket(FixSession& session, const std::string& msg) {
    if (session.socket && session.socket->is_open()) {
        session.socket->send(msg.c_str(), msg.size());
    }
}

std::string FixTcpAcceptor::add_poss_dup_flag(const std::string& fix_message) {
    // Don't add if already present
    if (fix_message.find("43=") != std::string::npos) {
        return fix_message;
    }
    // Insert 43=Y\x01 after the first SOH (after BeginString tag 8=FIX.x.x)
    auto first_soh = fix_message.find('\x01');
    if (first_soh == std::string::npos) {
        return fix_message;
    }
    return fix_message.substr(0, first_soh + 1) + "43=Y\x01" + fix_message.substr(first_soh + 1);
}

}  // namespace gateway
