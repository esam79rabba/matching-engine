#ifdef QUICKFIX_AVAILABLE

#include "gateway/quickfix_application.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <atomic>

namespace gateway {
namespace {

// ─── Test Helpers ────────────────────────────────────────────

/// Create a RealFixAdapter with trivial resolvers for testing.
RealFixAdapter make_test_adapter() {
    auto symbol_resolver = [](const std::string& sym) -> engine::InstrumentId {
        if (sym == "BTC-USD") return 1;
        if (sym == "ETH-USD") return 2;
        return 0;
    };
    auto price_converter = [](const std::string& p, engine::InstrumentId) -> engine::Price {
        return static_cast<engine::Price>(std::stod(p) * 100);
    };
    auto qty_converter = [](const std::string& q, engine::InstrumentId) -> engine::Quantity {
        return static_cast<engine::Quantity>(std::stod(q) * 100);
    };
    auto participant_resolver = [](const std::string& comp_id) -> engine::ParticipantId {
        if (comp_id == "MM01") return 100;
        if (comp_id == "MM02") return 200;
        return 0;
    };

    return RealFixAdapter(symbol_resolver, price_converter,
                          qty_converter, participant_resolver);
}

// ─── QuickFixApplication Construction ────────────────────────

TEST(QuickFixApplicationTest, ConstructionSucceeds) {
    auto adapter = make_test_adapter();
    QuickFixApplication app(adapter);
    EXPECT_EQ(app.session_count(), 0u);
}

TEST(QuickFixApplicationTest, SetCallbacksDoesNotThrow) {
    auto adapter = make_test_adapter();
    QuickFixApplication app(adapter);

    QuickFixCallbacks callbacks;
    callbacks.on_message = [](engine::SessionId, const std::string&) {};
    callbacks.on_logon = [](engine::SessionId, engine::ParticipantId) {};
    callbacks.on_logout = [](engine::SessionId) {};

    EXPECT_NO_THROW(app.set_callbacks(callbacks));
}

// ─── Session Mapping ─────────────────────────────────────────

TEST(QuickFixApplicationTest, SessionCreatedOnLogon) {
    auto adapter = make_test_adapter();
    QuickFixApplication app(adapter);

    std::atomic<engine::SessionId> logged_in_sid{0};
    QuickFixCallbacks callbacks;
    callbacks.on_logon = [&](engine::SessionId sid, engine::ParticipantId) {
        logged_in_sid.store(sid);
    };
    app.set_callbacks(callbacks);

    // Simulate a logon by calling onLogon with a FIX SessionID
    FIX::SessionID qf_session("FIX.4.4", "EXCHANGE", "MM01");
    app.onLogon(qf_session);

    EXPECT_GT(logged_in_sid.load(), 0u);
    EXPECT_EQ(app.session_count(), 1u);
}

TEST(QuickFixApplicationTest, SameSessionReusesId) {
    auto adapter = make_test_adapter();
    QuickFixApplication app(adapter);

    FIX::SessionID qf_session("FIX.4.4", "EXCHANGE", "MM01");
    app.onLogon(qf_session);
    const engine::SessionId first_sid = app.get_session_id(qf_session);

    // Logout and re-logon
    app.onLogout(qf_session);
    app.onLogon(qf_session);
    const engine::SessionId second_sid = app.get_session_id(qf_session);

    EXPECT_EQ(first_sid, second_sid);
}

TEST(QuickFixApplicationTest, DifferentSessionsGetDifferentIds) {
    auto adapter = make_test_adapter();
    QuickFixApplication app(adapter);

    FIX::SessionID session1("FIX.4.4", "EXCHANGE", "MM01");
    FIX::SessionID session2("FIX.4.4", "EXCHANGE", "MM02");

    app.onLogon(session1);
    app.onLogon(session2);

    const engine::SessionId sid1 = app.get_session_id(session1);
    const engine::SessionId sid2 = app.get_session_id(session2);

    EXPECT_NE(sid1, sid2);
    EXPECT_EQ(app.session_count(), 2u);
}

// ─── Logout Callback ─────────────────────────────────────────

TEST(QuickFixApplicationTest, LogoutCallbackInvoked) {
    auto adapter = make_test_adapter();
    QuickFixApplication app(adapter);

    std::atomic<engine::SessionId> logout_sid{0};
    QuickFixCallbacks callbacks;
    callbacks.on_logon = [](engine::SessionId, engine::ParticipantId) {};
    callbacks.on_logout = [&](engine::SessionId sid) {
        logout_sid.store(sid);
    };
    app.set_callbacks(callbacks);

    FIX::SessionID qf_session("FIX.4.4", "EXCHANGE", "MM01");
    app.onLogon(qf_session);
    const engine::SessionId expected_sid = app.get_session_id(qf_session);

    app.onLogout(qf_session);
    EXPECT_EQ(logout_sid.load(), expected_sid);
}

// ─── Unknown Session Returns Zero ────────────────────────────

TEST(QuickFixApplicationTest, UnknownSessionReturnsZero) {
    auto adapter = make_test_adapter();
    QuickFixApplication app(adapter);

    FIX::SessionID unknown("FIX.4.4", "EXCHANGE", "UNKNOWN_CLIENT");
    EXPECT_EQ(app.get_session_id(unknown), 0u);
}

// ─── Adapter Logon/Logout Counters ──────────────────────────

TEST(QuickFixApplicationTest, AdapterLogonCountIncremented) {
    auto adapter = make_test_adapter();
    QuickFixApplication app(adapter);

    FIX::SessionID qf_session("FIX.4.4", "EXCHANGE", "MM01");
    EXPECT_EQ(adapter.logon_count, 0u);

    app.onLogon(qf_session);
    EXPECT_EQ(adapter.logon_count, 1u);

    app.onLogout(qf_session);
    EXPECT_EQ(adapter.logout_count, 1u);
}

}  // namespace
}  // namespace gateway

#else // !QUICKFIX_AVAILABLE

#include <gtest/gtest.h>

TEST(QuickFixSkippedTest, QuickFixNotAvailable) {
    GTEST_SKIP() << "QuickFIX not available — skipping integration tests. "
                    "Build with -DENABLE_QUICKFIX=ON to enable.";
}

#endif // QUICKFIX_AVAILABLE
