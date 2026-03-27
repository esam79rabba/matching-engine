#include "refdata/ref_data_http.h"
#include <gtest/gtest.h>

namespace refdata {
namespace {

RefDataService make_service() {
    RefDataService svc;
    engine::InstrumentConfig cfg{};
    cfg.instrument_id = 100; cfg.tick_size = 1; cfg.lot_size = 1;
    cfg.min_quantity = 1; cfg.max_quantity = 1000000;
    cfg.static_ref_price = 50000; cfg.static_band_pct = 20.0;
    cfg.residual_policy = engine::ResidualPolicy::Cancel;
    cfg.stp_mode = engine::STPMode::CancelNewest;
    svc.add_instrument(cfg);
    svc.register_symbol("BTC-USD", 100);

    RefDataService::ParticipantInfo p{};
    p.id = 1; p.name = "TestBroker";
    svc.add_participant(p);

    RefDataService::SessionConfig s{};
    s.session_id = 10; s.participant_id = 1; s.cod_enabled = true;
    s.rate_limit.max_messages_per_window = 500;
    svc.add_session(s);

    return svc;
}

TEST(RefDataHttpTest, ListInstruments) {
    auto svc = make_service();
    RefDataHttpHandler handler(svc);
    auto resp = handler.handle({"GET", "/api/v1/instruments", ""});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"instrument_id\":100"), std::string::npos);
}

TEST(RefDataHttpTest, GetInstrumentById) {
    auto svc = make_service();
    RefDataHttpHandler handler(svc);
    auto resp = handler.handle({"GET", "/api/v1/instruments/100", ""});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"tick_size\":1"), std::string::npos);
}

TEST(RefDataHttpTest, GetInstrumentBySymbol) {
    auto svc = make_service();
    RefDataHttpHandler handler(svc);
    auto resp = handler.handle({"GET", "/api/v1/symbols/BTC-USD", ""});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"instrument_id\":100"), std::string::npos);
}

TEST(RefDataHttpTest, UnknownSymbolReturns404) {
    auto svc = make_service();
    RefDataHttpHandler handler(svc);
    auto resp = handler.handle({"GET", "/api/v1/symbols/UNKNOWN", ""});
    EXPECT_EQ(resp.status, 404);
}

TEST(RefDataHttpTest, UnknownInstrumentReturns404) {
    auto svc = make_service();
    RefDataHttpHandler handler(svc);
    auto resp = handler.handle({"GET", "/api/v1/instruments/999", ""});
    EXPECT_EQ(resp.status, 404);
}

TEST(RefDataHttpTest, ListParticipants) {
    auto svc = make_service();
    RefDataHttpHandler handler(svc);
    auto resp = handler.handle({"GET", "/api/v1/participants", ""});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("TestBroker"), std::string::npos);
}

TEST(RefDataHttpTest, GetSession) {
    auto svc = make_service();
    RefDataHttpHandler handler(svc);
    auto resp = handler.handle({"GET", "/api/v1/sessions/10", ""});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"cod_enabled\":true"), std::string::npos);
    EXPECT_NE(resp.body.find("\"rate_limit\":500"), std::string::npos);
}

TEST(RefDataHttpTest, UnknownRouteReturns404) {
    auto svc = make_service();
    RefDataHttpHandler handler(svc);
    auto resp = handler.handle({"GET", "/api/v1/nonexistent", ""});
    EXPECT_EQ(resp.status, 404);
}

TEST(RefDataHttpTest, PostMethodWithEmptyBodyRejects) {
    auto svc = make_service();
    RefDataHttpHandler handler(svc);
    auto resp = handler.handle({"POST", "/api/v1/instruments", ""});
    EXPECT_EQ(resp.status, 400);
}

// ─── B7: Create Instrument ───────────────────────────────────

TEST(RefDataHttpWriteTest, CreateInstrument) {
    auto svc = make_service();
    RefDataHttpHandler handler(svc);
    std::string body = R"({"instrument_id":102,"symbol":"SOL-USD","tick_size":1,"lot_size":1,)"
                       R"("min_quantity":1,"max_quantity":1000000,"static_ref_price":150,)"
                       R"("static_band_pct":10.0})";
    auto resp = handler.handle({"POST", "/api/v1/instruments", body});
    EXPECT_EQ(resp.status, 201);
    EXPECT_NE(resp.body.find("\"instrument_id\":102"), std::string::npos);
    EXPECT_NE(resp.body.find("\"status\":\"created\""), std::string::npos);

    // Verify instrument was actually created
    auto get_resp = handler.handle({"GET", "/api/v1/instruments/102", ""});
    EXPECT_EQ(get_resp.status, 200);
    EXPECT_NE(get_resp.body.find("\"instrument_id\":102"), std::string::npos);

    // Verify symbol resolution works
    auto sym_resp = handler.handle({"GET", "/api/v1/symbols/SOL-USD", ""});
    EXPECT_EQ(sym_resp.status, 200);
    EXPECT_NE(sym_resp.body.find("\"instrument_id\":102"), std::string::npos);
}

TEST(RefDataHttpWriteTest, UpdateInstrument) {
    auto svc = make_service();
    RefDataHttpHandler handler(svc);
    std::string body = R"({"tick_size":5,"static_ref_price":60000})";
    auto resp = handler.handle({"PUT", "/api/v1/instruments/100", body});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"tick_size\":5"), std::string::npos);
    EXPECT_NE(resp.body.find("\"static_ref_price\":60000"), std::string::npos);
    EXPECT_NE(resp.body.find("\"status\":\"updated\""), std::string::npos);
}

TEST(RefDataHttpWriteTest, UpdateNonexistentReturns404) {
    auto svc = make_service();
    RefDataHttpHandler handler(svc);
    std::string body = R"({"tick_size":5})";
    auto resp = handler.handle({"PUT", "/api/v1/instruments/999", body});
    EXPECT_EQ(resp.status, 404);
}

// ─── B8: Create Participant ──────────────────────────────────

TEST(RefDataHttpWriteTest, CreateParticipant) {
    auto svc = make_service();
    RefDataHttpHandler handler(svc);
    std::string body = R"({"participant_id":3,"name":"New Participant","sender_comp_id":"NEW_PART",)"
                       R"("max_order_rate":500,"fat_finger_price_pct":10.0,"fat_finger_max_notional":5000000})";
    auto resp = handler.handle({"POST", "/api/v1/participants", body});
    EXPECT_EQ(resp.status, 201);
    EXPECT_NE(resp.body.find("\"participant_id\":3"), std::string::npos);
    EXPECT_NE(resp.body.find("\"status\":\"created\""), std::string::npos);

    // Verify participant was created
    auto get_resp = handler.handle({"GET", "/api/v1/participants/3", ""});
    EXPECT_EQ(get_resp.status, 200);
    EXPECT_NE(get_resp.body.find("New Participant"), std::string::npos);
}

// ─── B10: Risk Limits Read ───────────────────────────────────

TEST(RefDataHttpWriteTest, RiskLimitsRead) {
    auto svc = make_service();

    // Add participant with specific risk limits
    RefDataService::ParticipantInfo p{};
    p.id = 5;
    p.name = "RiskTestBroker";
    p.risk_limits.max_order_qty = 5000;
    p.risk_limits.max_notional = 2000000.0;
    p.risk_limits.max_price_deviation_pct = 15.0;
    p.risk_limits.max_message_rate = 800;
    svc.add_participant(p);

    RefDataHttpHandler handler(svc);
    auto resp = handler.handle({"GET", "/api/v1/participants/5/risk-limits", ""});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"participant_id\":5"), std::string::npos);
    EXPECT_NE(resp.body.find("\"max_order_qty\":5000"), std::string::npos);
    EXPECT_NE(resp.body.find("\"max_message_rate\":800"), std::string::npos);
}

TEST(RefDataHttpWriteTest, RiskLimitsNotFound) {
    auto svc = make_service();
    RefDataHttpHandler handler(svc);
    auto resp = handler.handle({"GET", "/api/v1/participants/999/risk-limits", ""});
    EXPECT_EQ(resp.status, 404);
}

// ─── Validation ──────────────────────────────────────────────

TEST(RefDataHttpWriteTest, MissingFieldsReject) {
    auto svc = make_service();
    RefDataHttpHandler handler(svc);

    // Missing required instrument fields
    std::string body = R"({"instrument_id":200,"symbol":"BAD"})";
    auto resp = handler.handle({"POST", "/api/v1/instruments", body});
    EXPECT_EQ(resp.status, 400);
    EXPECT_NE(resp.body.find("Missing required fields"), std::string::npos);

    // Missing required participant fields
    std::string body2 = R"({"participant_id":10})";
    auto resp2 = handler.handle({"POST", "/api/v1/participants", body2});
    EXPECT_EQ(resp2.status, 400);
    EXPECT_NE(resp2.body.find("Missing required fields"), std::string::npos);
}

}  // namespace
}  // namespace refdata
