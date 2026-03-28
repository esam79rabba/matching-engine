#pragma once

#ifdef QUICKFIX_AVAILABLE

#include "gateway/fix_parser.h"
#include "gateway/fix_gateway_adapter.h"

// QuickFIX headers trigger warnings that our strict flags reject.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wignored-qualifiers"
#pragma clang diagnostic ignored "-Wdeprecated-copy-with-user-provided-copy"
#pragma clang diagnostic ignored "-Woverloaded-virtual"
#pragma clang diagnostic ignored "-Wdeprecated"

#include "Application.h"
#include "MessageCracker.h"
#include "Session.h"
#include "SessionID.h"
#include "SessionSettings.h"
#include "FileStore.h"
#include "FileLog.h"
#include "SocketAcceptor.h"

#pragma clang diagnostic pop

#include <functional>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <atomic>

namespace gateway {

/// Callback from QuickFIX application to the daemon.
struct QuickFixCallbacks {
    std::function<void(engine::SessionId, const std::string& raw_fix)> on_message;
    std::function<void(engine::SessionId, engine::ParticipantId)> on_logon;
    std::function<void(engine::SessionId)> on_logout;
};

/// Multi-version QuickFIX Application — supports FIX 4.0 through 5.0 SP2.
///
/// QuickFIX handles session management for ALL FIX versions:
///   - Logon, Logout, Heartbeat, TestRequest
///   - Sequence number tracking and gap recovery
///   - Checksum (tag 10), BodyLength (tag 9), BeginString (tag 8) validation
///   - SSL/TLS (if configured)
///
/// RealFixAdapter handles application-level translation:
///   - NewOrderSingle (D), OrderCancelRequest (F), OrderCancelReplaceRequest (G)
///   - Tags are largely identical across FIX 4.2/4.4/5.0 for order entry
///
/// Version negotiation is automatic — QuickFIX accepts whatever BeginString
/// the client sends if the session config allows it (TargetCompID=*).
class QuickFixApplication : public FIX::Application {
public:
    explicit QuickFixApplication(RealFixAdapter& adapter);

    void set_callbacks(const QuickFixCallbacks& callbacks) { callbacks_ = callbacks; }

    // ─── FIX::Application interface ──────────────────────────

    void onCreate(const FIX::SessionID& session_id) override;
    void onLogon(const FIX::SessionID& session_id) override;
    void onLogout(const FIX::SessionID& session_id) override;
    void toAdmin(FIX::Message& message, const FIX::SessionID& session_id) override;
    void toApp(FIX::Message& message, const FIX::SessionID& session_id) override;
    void fromAdmin(const FIX::Message& message, const FIX::SessionID& session_id) override;

    /// Called for ALL application messages regardless of FIX version.
    /// We use generic tag extraction rather than version-specific classes,
    /// so NewOrderSingle (MsgType=D) works the same in FIX 4.2, 4.4, and 5.0.
    void fromApp(const FIX::Message& message, const FIX::SessionID& session_id) override;

    // ─── Outbound ────────────────────────────────────────────

    bool send_to_session(engine::SessionId session_id, const std::string& fix_message);

    // ─── Session mapping ─────────────────────────────────────

    engine::SessionId get_session_id(const FIX::SessionID& qf_session) const;
    FIX::SessionID get_qf_session_id(engine::SessionId session_id) const;
    uint32_t session_count() const;

    /// Get the FIX version string for a session (e.g., "FIX.4.4", "FIXT.1.1")
    std::string get_session_version(engine::SessionId session_id) const;

    // ─── Supported FIX versions ──────────────────────────────
    /// QuickFIX supports all of these natively:
    ///   FIX.4.0, FIX.4.1, FIX.4.2, FIX.4.3, FIX.4.4
    ///   FIXT.1.1 (FIX 5.0 / 5.0 SP1 / 5.0 SP2)

private:
    RealFixAdapter& adapter_;
    QuickFixCallbacks callbacks_;

    mutable std::mutex sessions_mutex_;
    std::unordered_map<std::string, engine::SessionId> comp_to_session_;
    std::unordered_map<engine::SessionId, FIX::SessionID> session_to_qf_;
    engine::SessionId next_session_id_ = 1;

    static std::string message_to_string(const FIX::Message& message);
    static std::string get_sender_comp_id(const FIX::SessionID& session_id);
};

/// Convenience wrapper that owns the QuickFIX acceptor lifecycle.
/// Creates SocketAcceptor from a config file, starts/stops it.
class QuickFixAcceptor {
public:
    QuickFixAcceptor(QuickFixApplication& app, const std::string& config_path);
    ~QuickFixAcceptor();

    bool start();
    void stop();
    [[nodiscard]] bool is_running() const { return running_.load(); }

private:
    QuickFixApplication& app_;
    std::string config_path_;
    std::atomic<bool> running_{false};
    std::unique_ptr<FIX::SessionSettings> settings_;
    std::unique_ptr<FIX::FileStoreFactory> store_factory_;
    std::unique_ptr<FIX::FileLogFactory> log_factory_;
    std::unique_ptr<FIX::SocketAcceptor> acceptor_;
};

}  // namespace gateway

#endif // QUICKFIX_AVAILABLE
