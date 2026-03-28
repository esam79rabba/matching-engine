#ifdef QUICKFIX_AVAILABLE

#include "gateway/quickfix_application.h"
#include "Fields.h"

#include <iostream>

namespace gateway {

// ═══════════════════════════════════════════════════════════
// QuickFixApplication — multi-version FIX support
// ═══════════════════════════════════════════════════════════

QuickFixApplication::QuickFixApplication(RealFixAdapter& adapter)
    : adapter_(adapter) {}

void QuickFixApplication::onCreate(const FIX::SessionID& session_id) {
    std::cout << "[QuickFIX] Session created: " << session_id << "\n";
}

void QuickFixApplication::onLogon(const FIX::SessionID& session_id) {
    std::string sender = get_sender_comp_id(session_id);
    engine::SessionId sid;

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

    engine::ParticipantId pid = 0;
    if (adapter_.participant_resolver_) {
        pid = adapter_.participant_resolver_(sender);
    }

    adapter_.on_session_logon(sid, pid);

    if (callbacks_.on_logon) {
        callbacks_.on_logon(sid, pid);
    }

    std::cout << "[QuickFIX] Logon: " << sender
              << " (session=" << sid << ", version=" << session_id.getBeginString() << ")\n";
}

void QuickFixApplication::onLogout(const FIX::SessionID& session_id) {
    std::string sender = get_sender_comp_id(session_id);
    engine::SessionId sid = 0;

    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = comp_to_session_.find(sender);
        if (it != comp_to_session_.end()) {
            sid = it->second;
        }
    }

    if (sid > 0) {
        adapter_.on_session_logout(sid);
        if (callbacks_.on_logout) {
            callbacks_.on_logout(sid);
        }
    }

    std::cout << "[QuickFIX] Logout: " << sender << " (session=" << sid << ")\n";
}

void QuickFixApplication::toAdmin(FIX::Message&, const FIX::SessionID&) {}
void QuickFixApplication::toApp(FIX::Message&, const FIX::SessionID&) {}
void QuickFixApplication::fromAdmin(const FIX::Message&, const FIX::SessionID&) {}

void QuickFixApplication::fromApp(const FIX::Message& message,
                                   const FIX::SessionID& session_id) {
    // Generic handler — works with ANY FIX version (4.0 through 5.0 SP2).
    //
    // QuickFIX has already validated:
    //   - Checksum (tag 10)
    //   - BodyLength (tag 9)
    //   - BeginString (tag 8)
    //   - Sequence numbers (tag 34)
    //   - Heartbeat/TestRequest protocol
    //
    // Order entry tags are IDENTICAL across FIX versions:
    //   11 (ClOrdID), 55 (Symbol), 54 (Side), 40 (OrdType),
    //   38 (OrderQty), 44 (Price), 59 (TimeInForce), 41 (OrigClOrdID)
    //
    // We extract the raw FIX string and pass it to RealFixAdapter
    // for version-agnostic tag-level parsing.

    std::string raw = message_to_string(message);
    engine::SessionId sid = get_session_id(session_id);

    if (sid > 0 && callbacks_.on_message) {
        callbacks_.on_message(sid, raw);
    }
}

bool QuickFixApplication::send_to_session(engine::SessionId session_id,
                                           const std::string& fix_message) {
    FIX::SessionID qf_id;
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = session_to_qf_.find(session_id);
        if (it == session_to_qf_.end()) return false;
        qf_id = it->second;
    }

    try {
        FIX::Message msg(fix_message, false);
        FIX::Session::sendToTarget(msg, qf_id);
        return true;
    } catch (...) {
        return false;
    }
}

engine::SessionId QuickFixApplication::get_session_id(const FIX::SessionID& qf_session) const {
    std::string sender = get_sender_comp_id(qf_session);
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = comp_to_session_.find(sender);
    return (it != comp_to_session_.end()) ? it->second : 0;
}

FIX::SessionID QuickFixApplication::get_qf_session_id(engine::SessionId session_id) const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = session_to_qf_.find(session_id);
    if (it != session_to_qf_.end()) return it->second;
    return FIX::SessionID();
}

uint32_t QuickFixApplication::session_count() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return static_cast<uint32_t>(comp_to_session_.size());
}

std::string QuickFixApplication::get_session_version(engine::SessionId session_id) const {
    auto qf_id = get_qf_session_id(session_id);
    return qf_id.getBeginString().getString();
}

std::string QuickFixApplication::message_to_string(const FIX::Message& message) {
    return message.toString();
}

std::string QuickFixApplication::get_sender_comp_id(const FIX::SessionID& session_id) {
    return session_id.getSenderCompID().getString();
}

// ═══════════════════════════════════════════════════════════
// QuickFixAcceptor — lifecycle wrapper
// ═══════════════════════════════════════════════════════════

QuickFixAcceptor::QuickFixAcceptor(QuickFixApplication& app, const std::string& config_path)
    : app_(app), config_path_(config_path) {}

QuickFixAcceptor::~QuickFixAcceptor() { stop(); }

bool QuickFixAcceptor::start() {
    try {
        settings_ = std::make_unique<FIX::SessionSettings>(config_path_);
        store_factory_ = std::make_unique<FIX::FileStoreFactory>(*settings_);
        log_factory_ = std::make_unique<FIX::FileLogFactory>(*settings_);
        acceptor_ = std::make_unique<FIX::SocketAcceptor>(
            app_, *store_factory_, *settings_, *log_factory_);
        acceptor_->start();
        running_ = true;
        std::cout << "[QuickFIX] Acceptor started (config: " << config_path_ << ")\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[QuickFIX] Failed to start: " << e.what() << "\n";
        return false;
    }
}

void QuickFixAcceptor::stop() {
    if (running_ && acceptor_) {
        acceptor_->stop();
        running_ = false;
        std::cout << "[QuickFIX] Acceptor stopped\n";
    }
}

}  // namespace gateway

#endif // QUICKFIX_AVAILABLE
