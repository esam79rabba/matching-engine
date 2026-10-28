#include "ops/ops_controller.h"
#include "posttrade/trade_store.h"
#include "engine/journal_stub.h"
#include "risk/risk_service.h"
#include <gtest/gtest.h>

namespace ops {
namespace {

engine::InstrumentConfig make_order_api_config(engine::InstrumentId id = 100) {
    engine::InstrumentConfig cfg{};
    cfg.instrument_id = id;
    cfg.tick_size = 1;
    cfg.lot_size = 1;
    cfg.min_quantity = 1;
    cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000;
    cfg.static_band_pct = 20.0;
    cfg.dynamic_band_pct = 10.0;
    cfg.residual_policy = engine::ResidualPolicy::Cancel;
    cfg.collar_ticks = 10;
    cfg.stp_mode = engine::STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    return cfg;
}

// ─── #100 Order Submission Tests ─────────────────────────────

class OrderApiTest : public ::testing::Test {
protected:
    engine::JournalStub journal{10000};
    engine::Partition partition{1, 10000, &journal};
    posttrade::TradeStore trade_store;
    OpsController ctrl;

    void SetUp() override {
        partition.add_instrument(make_order_api_config(100));
        ctrl.register_partition(&partition);
        ctrl.set_trade_store(&trade_store);
    }
};

TEST_F(OrderApiTest, SubmitOrderViaRest) {
    OpsRequest req;
    req.method = "POST";
    req.path = "/api/v1/orders";
    req.body = R"({"instrument_id":100,"side":"buy","type":"limit","price":10000,"quantity":50,"participant_id":1,"tif":"day"})";

    auto resp = ctrl.handle(req);
    EXPECT_EQ(resp.status, 201) << "Body: " << resp.body;
    EXPECT_NE(resp.body.find("\"order_id\":"), std::string::npos);
    EXPECT_NE(resp.body.find("\"status\":\"accepted\""), std::string::npos);
}

TEST_F(OrderApiTest, SubmitOrderRejectedByPositionLimit) {
    risk::BasicRiskService risk_svc;
    risk::RiskLimits limits;
    limits.max_long_position = 10;  // order of 50 exceeds this
    risk_svc.update_limits(1, limits);
    ctrl.set_risk_service(&risk_svc);

    OpsRequest req;
    req.method = "POST";
    req.path = "/api/v1/orders";
    req.body = R"({"instrument_id":100,"side":"buy","type":"limit","price":10000,"quantity":50,"participant_id":1,"tif":"day"})";

    auto resp = ctrl.handle(req);
    EXPECT_EQ(resp.status, 400) << "Body: " << resp.body;
    EXPECT_NE(resp.body.find("risk"), std::string::npos);
    EXPECT_NE(resp.body.find("\"reject_code\":" +
              std::to_string(static_cast<int>(engine::RejectCode::PositionLimitBreached))),
              std::string::npos) << resp.body;
    // Order must not have reached the book
    EXPECT_EQ(partition.get_engine(100)->book().best_bid(), 0);
}

TEST_F(OrderApiTest, SubmitOrderRejectedByBlockedParticipant) {
    risk::BasicRiskService risk_svc;
    risk_svc.block_participant(1);
    ctrl.set_risk_service(&risk_svc);

    OpsRequest req;
    req.method = "POST";
    req.path = "/api/v1/orders";
    req.body = R"({"instrument_id":100,"side":"buy","type":"limit","price":10000,"quantity":5,"participant_id":1})";
    EXPECT_EQ(ctrl.handle(req).status, 400);
}

TEST_F(OrderApiTest, SubmitOrderPassesRiskCheckWhenWithinLimits) {
    risk::BasicRiskService risk_svc;
    ctrl.set_risk_service(&risk_svc);

    OpsRequest req;
    req.method = "POST";
    req.path = "/api/v1/orders";
    req.body = R"({"instrument_id":100,"side":"buy","type":"limit","price":10000,"quantity":5,"participant_id":1})";
    EXPECT_EQ(ctrl.handle(req).status, 201);
}

TEST_F(OrderApiTest, SubmitOrderMissingQuantityReturnsError) {
    OpsRequest req;
    req.method = "POST";
    req.path = "/api/v1/orders";
    req.body = R"({"instrument_id":100,"side":"buy","type":"limit","price":10000})";

    auto resp = ctrl.handle(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_NE(resp.body.find("missing or invalid quantity"), std::string::npos);
}

TEST_F(OrderApiTest, SubmitOrderInvalidSideReturnsError) {
    OpsRequest req;
    req.method = "POST";
    req.path = "/api/v1/orders";
    req.body = R"({"instrument_id":100,"side":"invalid","type":"limit","price":10000,"quantity":50,"participant_id":1})";

    auto resp = ctrl.handle(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_NE(resp.body.find("invalid side"), std::string::npos);
}

TEST_F(OrderApiTest, SubmitOrderNoPartitionsReturnsError) {
    OpsController empty_ctrl;
    OpsRequest req;
    req.method = "POST";
    req.path = "/api/v1/orders";
    req.body = R"({"instrument_id":100,"side":"buy","type":"limit","price":10000,"quantity":50})";

    auto resp = empty_ctrl.handle(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_NE(resp.body.find("no partitions configured"), std::string::npos);
}

TEST_F(OrderApiTest, SubmitSellOrder) {
    OpsRequest req;
    req.method = "POST";
    req.path = "/api/v1/orders";
    req.body = R"({"instrument_id":100,"side":"sell","type":"limit","price":10000,"quantity":30,"participant_id":2,"tif":"gtc"})";

    auto resp = ctrl.handle(req);
    EXPECT_EQ(resp.status, 201);
    EXPECT_NE(resp.body.find("\"status\":\"accepted\""), std::string::npos);
    EXPECT_NE(resp.body.find("\"action\":\"inserted\""), std::string::npos);
}

TEST_F(OrderApiTest, SubmitOrderAndMatch) {
    // Submit a resting buy
    OpsRequest buy_req;
    buy_req.method = "POST";
    buy_req.path = "/api/v1/orders";
    buy_req.body = R"({"instrument_id":100,"side":"buy","type":"limit","price":10000,"quantity":50,"participant_id":1})";
    auto buy_resp = ctrl.handle(buy_req);
    EXPECT_EQ(buy_resp.status, 201);

    // Submit a sell that crosses
    OpsRequest sell_req;
    sell_req.method = "POST";
    sell_req.path = "/api/v1/orders";
    sell_req.body = R"({"instrument_id":100,"side":"sell","type":"limit","price":10000,"quantity":50,"participant_id":2})";
    auto sell_resp = ctrl.handle(sell_req);
    EXPECT_EQ(sell_resp.status, 201);
    EXPECT_NE(sell_resp.body.find("\"action\":\"filled\""), std::string::npos);
    EXPECT_NE(sell_resp.body.find("\"fills\":1"), std::string::npos);
}

// ─── Cancel Tests ────────────────────────────────────────────

TEST_F(OrderApiTest, CancelOrderViaRest) {
    // First submit a resting order
    OpsRequest submit_req;
    submit_req.method = "POST";
    submit_req.path = "/api/v1/orders";
    submit_req.body = R"({"instrument_id":100,"side":"buy","type":"limit","price":10000,"quantity":50,"participant_id":1})";
    auto submit_resp = ctrl.handle(submit_req);
    EXPECT_EQ(submit_resp.status, 201);

    // Extract order_id from response
    auto pos = submit_resp.body.find("\"order_id\":");
    ASSERT_NE(pos, std::string::npos);
    auto num_start = submit_resp.body.find_first_of("0123456789", pos);
    auto num_end = submit_resp.body.find_first_not_of("0123456789", num_start);
    std::string order_id_str = submit_resp.body.substr(num_start, num_end - num_start);

    // Cancel it
    OpsRequest cancel_req;
    cancel_req.method = "POST";
    cancel_req.path = "/api/v1/orders/" + order_id_str + "/cancel";

    auto cancel_resp = ctrl.handle(cancel_req);
    EXPECT_EQ(cancel_resp.status, 200) << "Body: " << cancel_resp.body;
    EXPECT_NE(cancel_resp.body.find("\"status\":\"cancelled\""), std::string::npos);
}

TEST_F(OrderApiTest, CancelNonExistentOrderReturns404) {
    OpsRequest req;
    req.method = "POST";
    req.path = "/api/v1/orders/999999/cancel";

    auto resp = ctrl.handle(req);
    EXPECT_EQ(resp.status, 404);
}

// ─── #101 Trade Query Tests ─────────────────────────────────

TEST_F(OrderApiTest, QueryTradesByParticipant) {
    // Record some trades in the store
    posttrade::TradeRecord tr{};
    tr.trade_id = 1;
    tr.buy_order_id = 10;
    tr.sell_order_id = 20;
    tr.buyer_id = 1;
    tr.seller_id = 2;
    tr.instrument_id = 100;
    tr.price = 10000;
    tr.quantity = 50;
    tr.timestamp = 1000;
    trade_store.record_trade(tr);

    tr.trade_id = 2;
    tr.buyer_id = 3;
    tr.seller_id = 1;
    tr.price = 10100;
    tr.quantity = 30;
    tr.timestamp = 2000;
    trade_store.record_trade(tr);

    OpsRequest req;
    req.method = "GET";
    req.path = "/api/v1/trades";
    req.params["participant_id"] = "1";
    req.params["limit"] = "50";

    auto resp = ctrl.handle(req);
    EXPECT_EQ(resp.status, 200);
    // Participant 1 is buyer in trade 1 and seller in trade 2
    EXPECT_NE(resp.body.find("\"trade_id\":2"), std::string::npos);
    EXPECT_NE(resp.body.find("\"trade_id\":1"), std::string::npos);
}

TEST_F(OrderApiTest, QueryTradesByInstrument) {
    posttrade::TradeRecord tr{};
    tr.trade_id = 10;
    tr.instrument_id = 100;
    tr.price = 10000;
    tr.quantity = 50;
    tr.buyer_id = 1;
    tr.seller_id = 2;
    tr.timestamp = 1000;
    trade_store.record_trade(tr);

    OpsRequest req;
    req.method = "GET";
    req.path = "/api/v1/trades";
    req.params["instrument_id"] = "100";

    auto resp = ctrl.handle(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"trade_id\":10"), std::string::npos);
}

TEST_F(OrderApiTest, QueryTradesNoParamReturnsError) {
    OpsRequest req;
    req.method = "GET";
    req.path = "/api/v1/trades";

    auto resp = ctrl.handle(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_NE(resp.body.find("parameter required"), std::string::npos);
}

TEST_F(OrderApiTest, QueryTradesNoStoreReturnsError) {
    OpsController ctrl_no_store;
    ctrl_no_store.register_partition(&partition);

    OpsRequest req;
    req.method = "GET";
    req.path = "/api/v1/trades";
    req.params["participant_id"] = "1";

    auto resp = ctrl_no_store.handle(req);
    EXPECT_EQ(resp.status, 400);
    EXPECT_NE(resp.body.find("trade store not configured"), std::string::npos);
}

// ─── #101 Order Book Depth Test ──────────────────────────────

TEST_F(OrderApiTest, QueryOrderBook) {
    // Submit a few resting orders
    OpsRequest req1;
    req1.method = "POST";
    req1.path = "/api/v1/orders";
    req1.body = R"({"instrument_id":100,"side":"buy","type":"limit","price":9900,"quantity":100,"participant_id":1})";
    ctrl.handle(req1);

    OpsRequest req2;
    req2.method = "POST";
    req2.path = "/api/v1/orders";
    req2.body = R"({"instrument_id":100,"side":"buy","type":"limit","price":9900,"quantity":50,"participant_id":2})";
    ctrl.handle(req2);

    OpsRequest req3;
    req3.method = "POST";
    req3.path = "/api/v1/orders";
    req3.body = R"({"instrument_id":100,"side":"sell","type":"limit","price":10100,"quantity":75,"participant_id":3})";
    ctrl.handle(req3);

    // Query the book
    OpsRequest book_req;
    book_req.method = "GET";
    book_req.path = "/api/v1/instruments/100/book";

    auto resp = ctrl.handle(book_req);
    EXPECT_EQ(resp.status, 200) << "Body: " << resp.body;
    EXPECT_NE(resp.body.find("\"instrument_id\":100"), std::string::npos);
    EXPECT_NE(resp.body.find("\"bids\":["), std::string::npos);
    EXPECT_NE(resp.body.find("\"asks\":["), std::string::npos);
    // Bid level at 9900 with qty 150 (100+50) and count 2
    EXPECT_NE(resp.body.find("\"price\":9900"), std::string::npos);
    EXPECT_NE(resp.body.find("\"quantity\":150"), std::string::npos);
    EXPECT_NE(resp.body.find("\"count\":2"), std::string::npos);
    // Ask level at 10100
    EXPECT_NE(resp.body.find("\"price\":10100"), std::string::npos);
}

TEST_F(OrderApiTest, QueryOrderBookNotFound) {
    OpsRequest req;
    req.method = "GET";
    req.path = "/api/v1/instruments/999/book";

    auto resp = ctrl.handle(req);
    EXPECT_EQ(resp.status, 404);
}

// ─── Instrument Trades Endpoint ──────────────────────────────

TEST_F(OrderApiTest, QueryInstrumentTrades) {
    posttrade::TradeRecord tr{};
    tr.trade_id = 42;
    tr.instrument_id = 100;
    tr.price = 10000;
    tr.quantity = 25;
    tr.buyer_id = 1;
    tr.seller_id = 2;
    tr.timestamp = 5000;
    trade_store.record_trade(tr);

    OpsRequest req;
    req.method = "GET";
    req.path = "/api/v1/instruments/100/trades";

    auto resp = ctrl.handle(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"trade_id\":42"), std::string::npos);
    EXPECT_NE(resp.body.find("\"price\":10000"), std::string::npos);
}

// ─── Order Query Tests ───────────────────────────────────────

TEST_F(OrderApiTest, QueryOrdersByParticipant) {
    posttrade::OrderRecord rec{};
    rec.event = posttrade::OrderRecord::Event::New;
    rec.order_id = 123;
    rec.participant_id = 1;
    rec.instrument_id = 100;
    rec.side = engine::Side::Buy;
    rec.price = 10000;
    rec.quantity = 50;
    rec.timestamp = 1000;
    trade_store.record_order(rec);

    OpsRequest req;
    req.method = "GET";
    req.path = "/api/v1/orders";
    req.params["participant_id"] = "1";

    auto resp = ctrl.handle(req);
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"order_id\":123"), std::string::npos);
}

TEST_F(OrderApiTest, QueryOrderDetail) {
    // Submit a resting order first
    OpsRequest submit_req;
    submit_req.method = "POST";
    submit_req.path = "/api/v1/orders";
    submit_req.body = R"({"instrument_id":100,"side":"buy","type":"limit","price":9500,"quantity":100,"participant_id":5})";
    auto submit_resp = ctrl.handle(submit_req);
    EXPECT_EQ(submit_resp.status, 201);

    // Extract order_id
    auto pos = submit_resp.body.find("\"order_id\":");
    ASSERT_NE(pos, std::string::npos);
    auto num_start = submit_resp.body.find_first_of("0123456789", pos);
    auto num_end = submit_resp.body.find_first_not_of("0123456789", num_start);
    std::string order_id_str = submit_resp.body.substr(num_start, num_end - num_start);

    // Query detail
    OpsRequest detail_req;
    detail_req.method = "GET";
    detail_req.path = "/api/v1/orders/" + order_id_str;

    auto resp = ctrl.handle(detail_req);
    EXPECT_EQ(resp.status, 200) << "Body: " << resp.body;
    EXPECT_NE(resp.body.find("\"order_id\":" + order_id_str), std::string::npos);
    EXPECT_NE(resp.body.find("\"instrument_id\":100"), std::string::npos);
    EXPECT_NE(resp.body.find("\"remaining_qty\":100"), std::string::npos);
}

}  // namespace
}  // namespace ops
