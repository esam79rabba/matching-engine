#pragma once

#ifdef QUICKFIX_AVAILABLE

#include "gateway/fix_parser.h"
#include "gateway/fix_gateway_adapter.h"

// QuickFIX headers trigger warnings that our -Werror would reject.
// Suppress them for the third-party includes only.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wignored-qualifiers"
#pragma clang diagnostic ignored "-Wdeprecated-copy-with-user-provided-copy"
#pragma clang diagnostic ignored "-Woverloaded-virtual"

#include "Application.h"
#include "MessageCracker.h"
#include "Session.h"
#include "SessionID.h"
#include "fix44/NewOrderSingle.h"
#include "fix44/OrderCancelRequest.h"
#include "fix44/OrderCancelReplaceRequest.h"
#include "fix44/ExecutionReport.h"

#pragma clang diagnostic pop

#include <functional>
#include <string>
#include <unordered_map>
#include <mutex>

namespace gateway {

/// Callback from QuickFIX application to the daemon.
struct QuickFixCallbacks {
    /// Called when a valid application message is received.
    /// raw_fix is the FIX message as a tag=value SOH-delimited string.
    std::function<void(engine::SessionId, const std::string& raw_fix)> on_message;

    /// Called when a session logs on successfully.
    std::function<void(engine::SessionId, engine::ParticipantId)> on_logon;

    /// Called when a session logs out (clean or disconnect).
    std::function<void(engine::SessionId)> on_logout;
};

/// QuickFIX Application that delegates to RealFixAdapter for message translation.
///
/// Division of labor:
///   QuickFIX handles:  session management, heartbeats, sequence numbers,
///                      checksum validation, BodyLength validation, Logon/Logout.
///   RealFixAdapter handles:  FIX tag -> internal command translation
///                            (NewOrderCommand, CancelCommand, etc.)
///
/// The daemon wires this as:
///   QuickFixApplication qf_app(fix_adapter);
///   FIX::SocketAcceptor acceptor(qf_app, store, settings, log);
///   acceptor.start();
///
class QuickFixApplication : public FIX::Application, public FIX::MessageCracker {
public:
    explicit QuickFixApplication(RealFixAdapter& adapter);

    void set_callbacks(const QuickFixCallbacks& callbacks) { callbacks_ = callbacks; }

    // ─── FIX::Application interface ──────────────────────────

    /// Called when a new session is created.
    void onCreate(const FIX::SessionID& session_id) override;

    /// Called when a counterparty logs on.
    void onLogon(const FIX::SessionID& session_id) override;

    /// Called when a counterparty logs out or disconnects.
    void onLogout(const FIX::SessionID& session_id) override;

    /// Called before an admin message is sent (e.g., Heartbeat, Logon).
    void toAdmin(FIX::Message& message, const FIX::SessionID& session_id) override;

    /// Called before an application message is sent.
    void toApp(FIX::Message& message, const FIX::SessionID& session_id) override;

    /// Called when an admin message is received.
    void fromAdmin(const FIX::Message& message,
                   const FIX::SessionID& session_id) override;

    /// Called when an application message is received.
    /// QuickFIX has already validated checksum, BodyLength, and sequence numbers.
    void fromApp(const FIX::Message& message,
                 const FIX::SessionID& session_id) override;

    // ─── FIX::MessageCracker handlers ────────────────────────
    // We only override the three FIX 4.4 message types we handle.
    // The base MessageCracker has many onMessage overloads; suppress the
    // -Woverloaded-virtual warning for our intentional overrides.

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverloaded-virtual"

    void onMessage(const FIX44::NewOrderSingle& message,
                   const FIX::SessionID& session_id) override;

    void onMessage(const FIX44::OrderCancelRequest& message,
                   const FIX::SessionID& session_id) override;

    void onMessage(const FIX44::OrderCancelReplaceRequest& message,
                   const FIX::SessionID& session_id) override;

#pragma clang diagnostic pop

    // ─── Outbound ────────────────────────────────────────────

    /// Send a raw FIX message string to the session identified by our internal SessionId.
    /// Returns false if the session is not found or send fails.
    bool send_to_session(engine::SessionId session_id, const std::string& fix_message);

    // ─── Session mapping ─────────────────────────────────────

    /// Map a QuickFIX SessionID to our internal SessionId.
    engine::SessionId get_session_id(const FIX::SessionID& qf_session) const;

    /// Map our internal SessionId back to a QuickFIX SessionID.
    /// Returns empty string on failure.
    FIX::SessionID get_qf_session_id(engine::SessionId session_id) const;

    /// Number of active sessions.
    uint32_t session_count() const;

private:
    RealFixAdapter& adapter_;
    QuickFixCallbacks callbacks_;

    mutable std::mutex sessions_mutex_;

    // SenderCompID string -> our internal SessionId
    std::unordered_map<std::string, engine::SessionId> comp_to_session_;

    // Our internal SessionId -> QuickFIX SessionID (as string for map key)
    std::unordered_map<engine::SessionId, FIX::SessionID> session_to_qf_;

    engine::SessionId next_session_id_ = 1;

    /// Convert a QuickFIX Message to the raw FIX string our parser expects.
    static std::string message_to_string(const FIX::Message& message);

    /// Extract SenderCompID from a QuickFIX SessionID.
    static std::string get_sender_comp_id(const FIX::SessionID& session_id);
};

}  // namespace gateway

#endif // QUICKFIX_AVAILABLE
