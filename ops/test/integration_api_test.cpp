#include "ops/ops_controller.h"
#include "risk/risk_service.h"
#include "posttrade/trade_store.h"
#include "ops/audit_log.h"
#include <gtest/gtest.h>
#include <string>

using namespace ops;

namespace {

/// Helper to parse a simple JSON integer field from response body.
int64_t json_int(const std::string& json, const std::string& key) {
    auto key_pos = json.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return -999;
    auto colon = json.find(':', key_pos);
    if (colon == std::string::npos) return -999;
    auto start = colon + 1;
    while (start < json.size() && (json[start] == ' ' || json[start] == '\t')) ++start;
    try { return std::stoll(json.substr(start)); } catch (...) { return -999; }
}

std::string json_str(const std::string& json, const std::string& key) {
    auto key_pos = json.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return {};
    auto colon = json.find(':', key_pos);
    if (colon == std::string::npos) return {};
    auto q1 = json.find('"', colon);
    if (q1 == std::string::npos) return {};
    auto q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    return json.substr(q1 + 1, q2 - q1 - 1);
}

class IntegrationApiTest : public ::testing::Test {
protected:
    void SetUp() override {
        ctrl_.set_risk_service(&risk_svc_);
        ctrl_.set_trade_store(&trade_store_);
    }

    risk::BasicRiskService risk_svc_;
    posttrade::TradeStore trade_store_;
    OpsController ctrl_;
};

}  // namespace

TEST_F(IntegrationApiTest, SetCreditLimit) {
    OpsRequest req;
    req.method = "POST";
    req.path = "/api/v1/accounts/1/credit-limit";
    req.body = R"({"max_credit": 1000000})";

    auto resp = ctrl_.handle(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_EQ(json_int(resp.body, "participant_id"), 1);
    EXPECT_EQ(json_int(resp.body, "max_credit"), 1000000);

    // Verify the limit was actually set
    EXPECT_EQ(risk_svc_.credit_tracker().get_limit(1), 1000000);
}

TEST_F(IntegrationApiTest, GetCreditStatus) {
    // Set up credit state
    risk_svc_.credit_tracker().set_limit(1, 1000000);
    risk_svc_.credit_tracker().on_fill(1, 100, 2500);  // 250000 exposure

    OpsRequest req;
    req.method = "GET";
    req.path = "/api/v1/accounts/1/credit";

    auto resp = ctrl_.handle(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_EQ(json_int(resp.body, "participant_id"), 1);
    EXPECT_EQ(json_int(resp.body, "max_credit"), 1000000);
    EXPECT_EQ(json_int(resp.body, "current_exposure"), 250000);
    EXPECT_EQ(json_int(resp.body, "available"), 750000);
}

TEST_F(IntegrationApiTest, ResetCreditExposure) {
    // Set some exposure
    risk_svc_.credit_tracker().set_limit(1, 1000000);
    risk_svc_.credit_tracker().on_fill(1, 100, 2500);
    EXPECT_EQ(risk_svc_.credit_tracker().get_exposure(1), 250000);

    OpsRequest req;
    req.method = "POST";
    req.path = "/api/v1/accounts/1/credit-limit/reset";

    auto resp = ctrl_.handle(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_EQ(json_int(resp.body, "current_exposure"), 0);

    // Verify exposure is actually zero
    EXPECT_EQ(risk_svc_.credit_tracker().get_exposure(1), 0);
}

TEST_F(IntegrationApiTest, GetPositions) {
    // Simulate some fills
    risk_svc_.position_tracker().on_fill(1, 100, engine::Side::Buy, 50);
    risk_svc_.position_tracker().on_fill(1, 100, engine::Side::Sell, 20);
    risk_svc_.position_tracker().on_fill(1, 200, engine::Side::Buy, 10);

    OpsRequest req;
    req.method = "GET";
    req.path = "/api/v1/accounts/1/positions";

    auto resp = ctrl_.handle(req);
    EXPECT_EQ(resp.status, 200);
    // Should contain instrument 100 and 200 positions
    EXPECT_NE(resp.body.find("\"instrument_id\":100"), std::string::npos);
    EXPECT_NE(resp.body.find("\"instrument_id\":200"), std::string::npos);
}

TEST_F(IntegrationApiTest, ResetPositions) {
    risk_svc_.position_tracker().on_fill(1, 100, engine::Side::Buy, 50);
    auto pos_before = risk_svc_.position_tracker().get_position(1, 100);
    EXPECT_EQ(pos_before.net_qty, 50);

    OpsRequest req;
    req.method = "POST";
    req.path = "/api/v1/accounts/1/positions/reset";

    auto resp = ctrl_.handle(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("positions_reset"), std::string::npos);

    // Verify position is cleared
    auto pos_after = risk_svc_.position_tracker().get_position(1, 100);
    EXPECT_EQ(pos_after.net_qty, 0);
}

TEST_F(IntegrationApiTest, SettlementNotify) {
    // Record some trades first
    posttrade::TradeRecord rec{};
    rec.trade_id = 1;
    rec.buyer_id = 1;
    rec.seller_id = 2;
    rec.instrument_id = 100;
    rec.price = 100;
    rec.quantity = 10;
    trade_store_.record_trade(rec);

    OpsRequest req;
    req.method = "POST";
    req.path = "/api/v1/settlement/notify";
    req.body = R"({"trade_ids": [1,2,3], "status": "settled", "settlement_date": "2026-03-29"})";

    auto resp = ctrl_.handle(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_EQ(json_str(resp.body, "status"), "acknowledged");
    EXPECT_EQ(json_int(resp.body, "trade_count"), 3);
    EXPECT_EQ(json_str(resp.body, "settlement_status"), "settled");
    EXPECT_EQ(json_str(resp.body, "settlement_date"), "2026-03-29");
}

TEST_F(IntegrationApiTest, SettlementAdjustCredit) {
    // Set initial exposure
    risk_svc_.credit_tracker().set_limit(1, 1000000);
    risk_svc_.credit_tracker().on_fill(1, 100, 5000);  // 500000 exposure

    OpsRequest req;
    req.method = "POST";
    req.path = "/api/v1/settlement/adjust-credit";
    req.body = R"({"participant_id": 1, "adjustment": -500000, "reason": "settlement T+1"})";

    auto resp = ctrl_.handle(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_EQ(json_int(resp.body, "participant_id"), 1);
    EXPECT_EQ(json_int(resp.body, "adjustment"), -500000);
    EXPECT_EQ(json_int(resp.body, "new_exposure"), 0);
    EXPECT_EQ(json_str(resp.body, "reason"), "settlement T+1");
}

TEST_F(IntegrationApiTest, ReconciliationTrades) {
    // Record trades
    for (int i = 1; i <= 3; ++i) {
        posttrade::TradeRecord rec{};
        rec.trade_id = static_cast<engine::TradeId>(i);
        rec.buyer_id = 1;
        rec.seller_id = 2;
        rec.instrument_id = 100;
        rec.price = 100 * i;
        rec.quantity = 10;
        rec.timestamp = static_cast<engine::Timestamp>(1000 + i);
        trade_store_.record_trade(rec);
    }

    OpsRequest req;
    req.method = "GET";
    req.path = "/api/v1/reconciliation/trades";

    auto resp = ctrl_.handle(req);
    EXPECT_EQ(resp.status, 200);
    // Verify all 3 trades are present
    EXPECT_NE(resp.body.find("\"trade_id\":1"), std::string::npos);
    EXPECT_NE(resp.body.find("\"trade_id\":2"), std::string::npos);
    EXPECT_NE(resp.body.find("\"trade_id\":3"), std::string::npos);
    // Verify fees are included in reconciliation output
    EXPECT_NE(resp.body.find("\"buyer_fee\":"), std::string::npos);
}
