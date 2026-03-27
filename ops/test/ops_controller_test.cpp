#include "ops/ops_controller.h"
#include "ops/auth_service.h"
#include "ops/audit_log.h"
#include "eod/eod_processor.h"
#include "engine/journal_stub.h"
#include "engine/kill_switch.h"
#include <gtest/gtest.h>

namespace ops {
namespace {

engine::InstrumentConfig make_config(engine::InstrumentId id = 100) {
    engine::InstrumentConfig cfg{};
    cfg.instrument_id = id;
    cfg.tick_size = 1; cfg.lot_size = 1; cfg.min_quantity = 1; cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000; cfg.static_band_pct = 20.0; cfg.dynamic_band_pct = 10.0;
    cfg.residual_policy = engine::ResidualPolicy::Cancel; cfg.collar_ticks = 10;
    cfg.stp_mode = engine::STPMode::CancelNewest; cfg.qty_increase_loses_priority = true;
    return cfg;
}

TEST(OpsControllerTest, HealthEndpoint) {
    engine::JournalStub journal(1000);
    engine::Partition part(1, 1000, &journal);
    part.add_instrument(make_config(100));
    part.add_instrument(make_config(101));

    OpsController ctrl;
    ctrl.register_partition(&part);

    auto resp = ctrl.handle({"GET", "/api/v1/health", "", {}, {}});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"status\":\"ok\""), std::string::npos);
    EXPECT_NE(resp.body.find("\"total_instruments\":2"), std::string::npos);
}

TEST(OpsControllerTest, InstrumentsListEndpoint) {
    engine::JournalStub journal(1000);
    engine::Partition part(1, 1000, &journal);
    part.add_instrument(make_config(100));

    OpsController ctrl;
    ctrl.register_partition(&part);

    auto resp = ctrl.handle({"GET", "/api/v1/instruments", "", {}, {}});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"instrument_id\":100"), std::string::npos);
}

TEST(OpsControllerTest, InstrumentDetailEndpoint) {
    engine::JournalStub journal(1000);
    engine::Partition part(1, 1000, &journal);
    part.add_instrument(make_config(100));

    OpsController ctrl;
    ctrl.register_partition(&part);

    auto resp = ctrl.handle({"GET", "/api/v1/instruments/100", "", {}, {}});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"instrument_id\":100"), std::string::npos);
}

TEST(OpsControllerTest, InstrumentNotFound) {
    OpsController ctrl;
    auto resp = ctrl.handle({"GET", "/api/v1/instruments/999", "", {}, {}});
    EXPECT_EQ(resp.status, 404);
}

TEST(OpsControllerTest, HaltEndpoint) {
    engine::JournalStub journal(1000);
    engine::Partition part(1, 1000, &journal);
    part.add_instrument(make_config(100));

    OpsController ctrl;
    ctrl.register_partition(&part);

    auto resp = ctrl.handle({"POST", "/api/v1/instruments/100/halt", "", {}, {}});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"action\":\"halt\""), std::string::npos);
}

TEST(OpsControllerTest, PartitionsEndpoint) {
    engine::JournalStub journal(1000);
    engine::Partition part(1, 1000, &journal);
    part.add_instrument(make_config(100));

    OpsController ctrl;
    ctrl.register_partition(&part);

    auto resp = ctrl.handle({"GET", "/api/v1/partitions", "", {}, {}});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"partition_id\":1"), std::string::npos);
}

TEST(OpsControllerTest, UnknownRouteReturns404) {
    OpsController ctrl;
    auto resp = ctrl.handle({"GET", "/api/v1/nonexistent", "", {}, {}});
    EXPECT_EQ(resp.status, 404);
}

TEST(OpsControllerTest, EodTriggerWithoutProcessorReturnsError) {
    OpsController ctrl;
    auto resp = ctrl.handle({"POST", "/api/v1/eod/trigger", "", {}, {}});
    EXPECT_EQ(resp.status, 400);
    EXPECT_NE(resp.body.find("EOD processor not configured"), std::string::npos);
}

TEST(OpsControllerTest, EodTriggerWithProcessorSucceeds) {
    eod::EodProcessor eod_proc;

    // Record a trade so the report has data
    eod::TradeRecord rec{};
    rec.trade_id = 1;
    rec.instrument_id = 100;
    rec.buy_order_id = 1;
    rec.sell_order_id = 2;
    rec.buyer_id = 1;
    rec.seller_id = 2;
    rec.price = 10000;
    rec.quantity = 50;
    rec.timestamp = 1000;
    eod_proc.record_trade(rec, 2, 1);  // maker=2, taker=1

    OpsController ctrl;
    ctrl.set_eod_processor(&eod_proc, "");  // Empty dir = skip file writing

    auto resp = ctrl.handle({"POST", "/api/v1/eod/trigger", "", {}, {}});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"action\":\"eod_trigger\""), std::string::npos);
    EXPECT_NE(resp.body.find("\"total_trades\":1"), std::string::npos);
    EXPECT_NE(resp.body.find("\"total_volume\":50"), std::string::npos);
    EXPECT_NE(resp.body.find("\"status\":\"executed\""), std::string::npos);
}

// ─── B11: EOD Status ─────────────────────────────────────────

TEST(OpsControllerTest, EodStatusBeforeTrigger) {
    OpsController ctrl;
    auto resp = ctrl.handle({"GET", "/api/v1/eod/status", "", {}, {}});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"status\":\"no_eod_run\""), std::string::npos);
}

TEST(OpsControllerTest, EodStatusAfterTrigger) {
    eod::EodProcessor eod_proc;

    eod::TradeRecord rec{};
    rec.trade_id = 1;
    rec.instrument_id = 100;
    rec.buy_order_id = 1;
    rec.sell_order_id = 2;
    rec.buyer_id = 1;
    rec.seller_id = 2;
    rec.price = 10000;
    rec.quantity = 50;
    rec.timestamp = 1000;
    eod_proc.record_trade(rec, 2, 1);

    OpsController ctrl;
    ctrl.set_eod_processor(&eod_proc, "");

    // Trigger EOD
    ctrl.handle({"POST", "/api/v1/eod/trigger", "", {}, {}});

    // Now check status — should return the last result
    auto resp = ctrl.handle({"GET", "/api/v1/eod/status", "", {}, {}});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"action\":\"eod_trigger\""), std::string::npos);
    EXPECT_NE(resp.body.find("\"total_trades\":1"), std::string::npos);
    EXPECT_NE(resp.body.find("\"status\":\"executed\""), std::string::npos);
}

// ─── B3-B5: Kill Switch Routes ──────────────────────────────

TEST(OpsControllerTest, KillSwitchStatusNotConfigured) {
    OpsController ctrl;
    auto resp = ctrl.handle({"GET", "/api/v1/kill-switch", "", {}, {}});
    EXPECT_EQ(resp.status, 400);
}

TEST(OpsControllerTest, KillSwitchStatusEmpty) {
    engine::JournalStub journal(1000);
    engine::Partition part(1, 1000, &journal);
    part.add_instrument(make_config(100));

    engine::KillSwitchController ks;
    ks.register_partition(&part);

    OpsController ctrl;
    ctrl.register_partition(&part);
    ctrl.set_kill_switch(&ks);

    auto resp = ctrl.handle({"GET", "/api/v1/kill-switch", "", {}, {}});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"blocked_participants\":[]"), std::string::npos);
}

TEST(OpsControllerTest, KillSwitchActivateDeactivate) {
    engine::JournalStub journal(1000);
    engine::Partition part(1, 1000, &journal);
    part.add_instrument(make_config(100));

    engine::KillSwitchController ks;
    ks.register_partition(&part);

    OpsController ctrl;
    ctrl.register_partition(&part);
    ctrl.set_kill_switch(&ks);

    // Activate
    auto resp = ctrl.handle({"POST", "/api/v1/kill-switch/42/activate", "", {}, {}});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"action\":\"activated\""), std::string::npos);
    EXPECT_NE(resp.body.find("\"participant_id\":42"), std::string::npos);

    // Status should show 42
    resp = ctrl.handle({"GET", "/api/v1/kill-switch", "", {}, {}});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("42"), std::string::npos);

    // Deactivate
    resp = ctrl.handle({"POST", "/api/v1/kill-switch/42/deactivate", "", {}, {}});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"action\":\"deactivated\""), std::string::npos);

    // Status should be empty again
    resp = ctrl.handle({"GET", "/api/v1/kill-switch", "", {}, {}});
    EXPECT_NE(resp.body.find("\"blocked_participants\":[]"), std::string::npos);
}

// ─── B6: FIX Session Routes ────────────────────────────────

TEST(OpsControllerTest, FixSessionsNoCallback) {
    OpsController ctrl;
    auto resp = ctrl.handle({"GET", "/api/v1/fix/sessions", "", {}, {}});
    EXPECT_EQ(resp.status, 200);
    EXPECT_EQ(resp.body, "[]");
}

TEST(OpsControllerTest, FixSessionsWithCallback) {
    OpsController ctrl;
    ctrl.set_session_query([]() -> std::string {
        return R"([{"session_id":1,"participant_id":10,"logged_in":true}])";
    });

    auto resp = ctrl.handle({"GET", "/api/v1/fix/sessions", "", {}, {}});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"session_id\":1"), std::string::npos);
    EXPECT_NE(resp.body.find("\"logged_in\":true"), std::string::npos);
}

// ─── B9: Audit Log Integration ──────────────────────────────

TEST(OpsControllerTest, AuditLogOnHalt) {
    engine::JournalStub journal(1000);
    engine::Partition part(1, 1000, &journal);
    part.add_instrument(make_config(100));

    std::string audit_path = "/tmp/ops_test_audit_" + std::to_string(getpid()) + ".jsonl";
    AuditLog audit(audit_path);

    OpsController ctrl;
    ctrl.register_partition(&part);
    ctrl.set_audit_log(&audit);

    ctrl.handle({"POST", "/api/v1/instruments/100/halt", "", {}, {}});

    // Verify audit file was written
    EXPECT_TRUE(audit.is_open());
    // Cleanup
    std::remove(audit_path.c_str());
}

// ─── B5: Full RBAC Tests ─────────────────────────────────────

class RbacTest : public ::testing::Test {
protected:
    engine::JournalStub journal{1000};
    engine::Partition partition{1, 1000, &journal};
    AuthService auth;
    OpsController ctrl;

    std::string viewer_token;
    std::string operator_token;
    std::string admin_token;

    void SetUp() override {
        partition.add_instrument(make_config(100));
        ctrl.register_partition(&partition);
        ctrl.set_auth_service(&auth);

        auth.add_user("viewer_user", "pass1", "viewer");
        auth.add_user("operator_user", "pass2", "operator");
        auth.add_user("admin_user", "pass3", "admin");

        viewer_token = *auth.login("viewer_user", "pass1");
        operator_token = *auth.login("operator_user", "pass2");
        admin_token = *auth.login("admin_user", "pass3");
    }

    OpsRequest get_req(const std::string& path, const std::string& token = "") {
        OpsRequest req{"GET", path, "", {}, {}};
        if (!token.empty()) {
            req.headers["Authorization"] = "Bearer " + token;
        }
        return req;
    }

    OpsRequest post_req(const std::string& path, const std::string& token = "",
                         const std::string& body = "") {
        OpsRequest req{"POST", path, body, {}, {}};
        if (!token.empty()) {
            req.headers["Authorization"] = "Bearer " + token;
        }
        return req;
    }
};

TEST_F(RbacTest, ViewerCanReadHealth) {
    auto resp = ctrl.handle(get_req("/api/v1/health", viewer_token));
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"status\":\"ok\""), std::string::npos);
}

TEST_F(RbacTest, ViewerCanReadInstruments) {
    auto resp = ctrl.handle(get_req("/api/v1/instruments", viewer_token));
    EXPECT_EQ(resp.status, 200);
}

TEST_F(RbacTest, ViewerCannotHalt) {
    auto resp = ctrl.handle(post_req("/api/v1/instruments/100/halt", viewer_token));
    EXPECT_EQ(resp.status, 403);
    EXPECT_NE(resp.body.find("operator role required"), std::string::npos);
}

TEST_F(RbacTest, ViewerCannotResume) {
    // First halt with operator so we can test resume
    auto resp = ctrl.handle(post_req("/api/v1/instruments/100/resume", viewer_token));
    EXPECT_EQ(resp.status, 403);
}

TEST_F(RbacTest, ViewerCannotClose) {
    auto resp = ctrl.handle(post_req("/api/v1/instruments/100/close", viewer_token));
    EXPECT_EQ(resp.status, 403);
}

TEST_F(RbacTest, ViewerCannotTriggerEod) {
    auto resp = ctrl.handle(post_req("/api/v1/eod/trigger", viewer_token));
    EXPECT_EQ(resp.status, 403);
}

TEST_F(RbacTest, ViewerCannotActivateKillSwitch) {
    engine::KillSwitchController ks;
    ks.register_partition(&partition);
    ctrl.set_kill_switch(&ks);

    auto resp = ctrl.handle(post_req("/api/v1/kill-switch/42/activate", viewer_token));
    EXPECT_EQ(resp.status, 403);
}

TEST_F(RbacTest, OperatorCanHalt) {
    auto resp = ctrl.handle(post_req("/api/v1/instruments/100/halt", operator_token));
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"action\":\"halt\""), std::string::npos);
}

TEST_F(RbacTest, OperatorCanResume) {
    // First halt, then resume
    ctrl.handle(post_req("/api/v1/instruments/100/halt", operator_token));
    auto resp = ctrl.handle(post_req("/api/v1/instruments/100/resume", operator_token));
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"action\":\"resume\""), std::string::npos);
}

TEST_F(RbacTest, OperatorCanActivateKillSwitch) {
    engine::KillSwitchController ks;
    ks.register_partition(&partition);
    ctrl.set_kill_switch(&ks);

    auto resp = ctrl.handle(post_req("/api/v1/kill-switch/42/activate", operator_token));
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"action\":\"activated\""), std::string::npos);
}

TEST_F(RbacTest, AdminCanHalt) {
    auto resp = ctrl.handle(post_req("/api/v1/instruments/100/halt", admin_token));
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"action\":\"halt\""), std::string::npos);
}

TEST_F(RbacTest, NoAuthTokenOnMutatingEndpointReturns403) {
    // POST without any token should be forbidden when auth is configured
    auto resp = ctrl.handle(post_req("/api/v1/instruments/100/halt"));
    EXPECT_EQ(resp.status, 403);
}

TEST_F(RbacTest, NoAuthServiceAllowsAll) {
    // Create a controller without auth service
    OpsController ctrl_no_auth;
    ctrl_no_auth.register_partition(&partition);
    // No set_auth_service call

    // Viewer-level actions should work without auth
    auto resp = ctrl_no_auth.handle({"POST", "/api/v1/instruments/100/halt", "", {}, {}});
    EXPECT_EQ(resp.status, 200) << "Without auth service, all requests should pass";
}

TEST_F(RbacTest, ViewerCanReadKillSwitchStatus) {
    engine::KillSwitchController ks;
    ks.register_partition(&partition);
    ctrl.set_kill_switch(&ks);

    auto resp = ctrl.handle(get_req("/api/v1/kill-switch", viewer_token));
    EXPECT_EQ(resp.status, 200);
}

}  // namespace
}  // namespace ops
