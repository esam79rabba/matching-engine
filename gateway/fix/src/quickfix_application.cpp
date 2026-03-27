#ifdef QUICKFIX_AVAILABLE

#include "gateway/quickfix_application.h"

#include <iostream>
#include <sstream>

namespace gateway {

QuickFixApplication::QuickFixApplication(RealFixAdapter& adapter)
    : adapter_(adapter) {}

// ─── FIX::Application interface ──────────────────────────────

void QuickFixApplication::onCreate(const FIX::SessionID& session_id) {
    // Session created — no action needed until logon.
    (void)session_id;
}

void QuickFixApplication::onLogon(const FIX::SessionID& session_id) {
    const auto sender = get_sender_comp_id(session_id);
    engine::SessionId sid = 0;

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = comp_to_session_.find(sender);
        if (it != comp_to_session_.end()) {
            sid = it->second;
        } else {
            sid = next_session_id_++;
            comp_to_session_[sender] = sid;
            session_to_qf_[sid] = session_id;
        }
    }

    // Resolve participant from SenderCompID via the adapter's resolver.
    // For now, use 0 — the daemon's on_logon callback typically resolves this.
    engine::ParticipantId participant = 0;

    adapter_.on_session_logon(sid, participant);

    if (callbacks_.on_logon) {
        callbacks_.on_logon(sid, participant);
    }
}

void QuickFixApplication::onLogout(const FIX::SessionID& session_id) {
    const engine::SessionId sid = get_session_id(session_id);

    adapter_.on_session_logout(sid);

    if (callbacks_.on_logout) {
        callbacks_.on_logout(sid);
    }
}

void QuickFixApplication::toAdmin(FIX::Message& message,
                                   const FIX::SessionID& session_id) {
    // Could customize Logon message here (e.g., add password field).
    (void)message;
    (void)session_id;
}

void QuickFixApplication::toApp(FIX::Message& message,
                                 const FIX::SessionID& session_id) {
    // Called before sending application messages. No customization needed.
    (void)message;
    (void)session_id;
}

void QuickFixApplication::fromAdmin(const FIX::Message& message,
                                     const FIX::SessionID& session_id) {
    // Admin messages (Heartbeat, TestRequest, etc.) handled by QuickFIX.
    (void)message;
    (void)session_id;
}

void QuickFixApplication::fromApp(const FIX::Message& message,
                                   const FIX::SessionID& session_id) {
    // QuickFIX has already validated checksum, BodyLength, sequence numbers.
    // Crack the message to dispatch to the appropriate onMessage handler.
    crack(message, session_id);
}

// ─── FIX::MessageCracker handlers ────────────────────────────

void QuickFixApplication::onMessage(const FIX44::NewOrderSingle& message,
                                     const FIX::SessionID& session_id) {
    const engine::SessionId sid = get_session_id(session_id);
    const std::string raw = message_to_string(message);

    if (callbacks_.on_message) {
        callbacks_.on_message(sid, raw);
    }
}

void QuickFixApplication::onMessage(const FIX44::OrderCancelRequest& message,
                                     const FIX::SessionID& session_id) {
    const engine::SessionId sid = get_session_id(session_id);
    const std::string raw = message_to_string(message);

    if (callbacks_.on_message) {
        callbacks_.on_message(sid, raw);
    }
}

void QuickFixApplication::onMessage(const FIX44::OrderCancelReplaceRequest& message,
                                     const FIX::SessionID& session_id) {
    const engine::SessionId sid = get_session_id(session_id);
    const std::string raw = message_to_string(message);

    if (callbacks_.on_message) {
        callbacks_.on_message(sid, raw);
    }
}

// ─── Outbound ────────────────────────────────────────────────

bool QuickFixApplication::send_to_session(engine::SessionId session_id,
                                           const std::string& fix_message) {
    FIX::SessionID qf_session_id;

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = session_to_qf_.find(session_id);
        if (it == session_to_qf_.end()) {
            return false;
        }
        qf_session_id = it->second;
    }

    try {
        // Parse the raw FIX string into a QuickFIX Message and send it.
        FIX::Message msg(fix_message, false);  // false = don't validate
        return FIX::Session::sendToTarget(msg, qf_session_id);
    } catch (const std::exception&) {
        return false;
    }
}

// ─── Session mapping ─────────────────────────────────────────

engine::SessionId QuickFixApplication::get_session_id(
        const FIX::SessionID& qf_session) const {
    const auto sender = get_sender_comp_id(qf_session);
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = comp_to_session_.find(sender);
    if (it != comp_to_session_.end()) {
        return it->second;
    }
    return 0;  // Unknown session
}

FIX::SessionID QuickFixApplication::get_qf_session_id(
        engine::SessionId session_id) const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = session_to_qf_.find(session_id);
    if (it != session_to_qf_.end()) {
        return it->second;
    }
    return FIX::SessionID();
}

uint32_t QuickFixApplication::session_count() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return static_cast<uint32_t>(comp_to_session_.size());
}

// ─── Helpers ─────────────────────────────────────────────────

std::string QuickFixApplication::message_to_string(const FIX::Message& message) {
    return message.toString();
}

std::string QuickFixApplication::get_sender_comp_id(const FIX::SessionID& session_id) {
    return session_id.getTargetCompID().getString();
    // Note: in QuickFIX, the "Target" of the session is the remote party
    // (the client's SenderCompID). When the exchange is the acceptor,
    // SessionID.targetCompID is the client's SenderCompID.
}

}  // namespace gateway

#endif // QUICKFIX_AVAILABLE
