#pragma once

#include "engine/types.h"
#include "gateway/fix_parser.h"
#include "gateway/fix_gap_handler.h"
#include "gateway/fix_session_store.h"
#include "gateway/fix_message_store.h"
#include "transport/socket.h"

#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <memory>
#include <optional>

namespace gateway {

/// FIX session state for a connected client.
struct FixSession {
    engine::SessionId     session_id;
    engine::ParticipantId participant_id;
    transport::ISocket*   socket = nullptr;   // owned by the acceptor
    int                   socket_fd = -1;      // cached fd for backward compat
    bool                  logged_in = false;
    uint64_t              inbound_seq = 1;
    uint64_t              outbound_seq = 1;
    uint32_t              heartbeat_interval_sec = 30;
    std::string           sender_comp_id;
    std::string           target_comp_id;
};

/// Callbacks from the TCP acceptor to the gateway application layer.
struct FixTcpCallbacks {
    /// Called when a valid application message is received (after session handling).
    /// raw_fix is the complete FIX message string.
    std::function<void(engine::SessionId, const std::string& raw_fix)> on_message;

    /// Called when a session logs on successfully.
    std::function<void(engine::SessionId, engine::ParticipantId)> on_logon;

    /// Called when a session logs out (clean).
    std::function<void(engine::SessionId)> on_logout;

    /// Called when a session disconnects unexpectedly.
    std::function<void(engine::SessionId)> on_disconnect;

    /// Called when we emit a ResendRequest to the client.
    std::function<void(engine::SessionId, const std::string& resend_req)> on_resend_request;
};

/// Resolves a FIX SenderCompID (tag 49) to a participant. Returns nullopt for
/// unknown CompIDs, in which case the Logon is rejected.
using CompIdResolver =
    std::function<std::optional<engine::ParticipantId>(const std::string& sender_comp_id)>;

/// Lightweight FIX TCP acceptor — prototype session transport.
///
/// Handles:
///   - TCP accept on a listen port (plain TCP or TLS via transport abstraction)
///   - FIX Logon/Logout/Heartbeat protocol
///   - Session sequence tracking (basic)
///   - Message routing to application callbacks
///   - Disconnect detection
///   - Optional session state persistence via FixSessionStore
///
/// This is NOT QuickFIX. It is a minimal FIX session handler sufficient
/// for prototype integration testing. When QuickFIX is integrated (Phase 2+),
/// this class is replaced — the adapter boundary (RealFixAdapter) stays the same.
///
/// Generic — no asset-class-specific logic.
class FixTcpAcceptor {
public:
    explicit FixTcpAcceptor(uint16_t port);
    ~FixTcpAcceptor();

    /// Set application callbacks.
    void set_callbacks(const FixTcpCallbacks& callbacks) { callbacks_ = callbacks; }

    /// Set the SenderCompID -> participant_id resolver. Must be set before start().
    /// Fail-closed: with no resolver installed, every Logon is rejected.
    void set_comp_id_resolver(CompIdResolver resolver) { comp_id_resolver_ = std::move(resolver); }

    /// Inject a custom server socket (e.g., TLS). Acceptor does NOT own it.
    /// Must be called before start(). If not called, a TcpServerSocket is created internally.
    void set_server_socket(transport::IServerSocket* server);

    /// Convenience: configure TLS. Creates a TlsServerSocket internally during start().
    /// Must be called before start(). Ignored if set_server_socket() was already called.
    void set_tls_config(const transport::TlsConfig& config);

    /// Set session store for persistence. Non-owning pointer. Optional.
    void set_session_store(FixSessionStore* store);

    /// Set message store for replay on ResendRequest. Non-owning pointer. Optional.
    void set_message_store(FixMessageStore* store);

    /// Start accepting connections (blocking — call from a dedicated thread).
    /// Returns false if bind/listen fails.
    bool start();

    /// Stop accepting and close all sessions.
    void stop();

    /// Send a FIX message to a session.
    bool send(engine::SessionId session_id, const std::string& fix_message);

    /// Query active sessions.
    [[nodiscard]] uint32_t session_count() const;

    /// Return the ParticipantId bound to a logged-in session, or 0 if the
    /// session is unknown or not logged in. Used by the daemon's on_message
    /// callback to source participant_id from session context rather than
    /// from the inbound FIX message (which could be spoofed).
    [[nodiscard]] engine::ParticipantId participant_id_for_session(engine::SessionId sid) const;

    /// Return JSON array of active session info for the ops portal.
    [[nodiscard]] std::string sessions_json() const;

    /// Check if running.
    [[nodiscard]] bool is_running() const { return running_.load(); }

private:
    uint16_t port_;
    std::atomic<bool> running_{false};
    FixTcpCallbacks callbacks_;
    CompIdResolver comp_id_resolver_;

    // Server socket — either injected or created internally
    transport::IServerSocket* server_socket_ = nullptr;
    bool owns_server_socket_ = false;  // true if we created it internally

    // Optional TLS config (used only when no server socket is injected)
    bool has_tls_config_ = false;
    transport::TlsConfig tls_config_;

    // Session store for persistence (non-owning, optional)
    FixSessionStore* session_store_ = nullptr;

    // Message store for replay on ResendRequest (non-owning, optional)
    FixMessageStore* message_store_ = nullptr;

    mutable std::mutex sessions_mutex_;
    std::unordered_map<engine::SessionId, FixSession> sessions_;
    engine::SessionId next_session_id_ = 1;

    // Owns all client sockets
    mutable std::mutex sockets_mutex_;
    std::vector<transport::ISocket*> owned_sockets_;

    // Active client thread tracking for clean shutdown
    std::atomic<uint32_t> active_clients_{0};

    void handle_client(transport::ISocket* client_socket);
    void route_message(engine::SessionId sid, const std::string& msg_type,
                       const std::string& raw,
                       const std::unordered_map<int, std::string>& fields);
    bool process_logon(FixSession& session, const std::unordered_map<int, std::string>& fields);
    void reject_logon(FixSession& session, const std::string& text);
    void handle_heartbeat(FixSession& session);
    void handle_logout(FixSession& session);
    void close_session(engine::SessionId session_id);
    void persist_session(const FixSession& session);
    void check_inbound_sequence(FixSession& session, uint64_t received_seq);
    void handle_sequence_reset(FixSession& session, const std::string& raw_fix);
    void handle_resend_request(FixSession& session, const std::string& raw_fix);
    void send_to_socket(FixSession& session, const std::string& msg);

public:
    /// Add PossDupFlag (tag 43=Y) to a FIX message for replay.
    /// Inserts 43=Y after the BeginString tag (tag 8) if not already present.
    static std::string add_poss_dup_flag(const std::string& fix_message);
};

}  // namespace gateway
