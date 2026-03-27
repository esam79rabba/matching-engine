#include "refdata/ref_data_service.h"
#include <gtest/gtest.h>

namespace refdata {
namespace {

engine::InstrumentConfig make_config(engine::InstrumentId id) {
    engine::InstrumentConfig cfg{};
    cfg.instrument_id = id; cfg.tick_size = 1; cfg.lot_size = 1;
    cfg.min_quantity = 1; cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000; cfg.static_band_pct = 20.0;
    cfg.residual_policy = engine::ResidualPolicy::Cancel;
    cfg.stp_mode = engine::STPMode::CancelNewest;
    return cfg;
}

TEST(RefDataServiceTest, AddAndGetInstrument) {
    RefDataService svc;
    svc.add_instrument(make_config(100));

    auto cfg = svc.get_instrument(100);
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->instrument_id, 100u);
    EXPECT_EQ(cfg->tick_size, 1);
}

TEST(RefDataServiceTest, GetMissingInstrumentReturnsNullopt) {
    RefDataService svc;
    EXPECT_FALSE(svc.get_instrument(999).has_value());
}

TEST(RefDataServiceTest, SymbolResolution) {
    RefDataService svc;
    svc.add_instrument(make_config(100));
    svc.register_symbol("BTC-USD", 100);

    EXPECT_EQ(svc.resolve_symbol("BTC-USD"), 100u);
    EXPECT_EQ(svc.resolve_symbol("UNKNOWN"), 0u);
}

TEST(RefDataServiceTest, ListInstruments) {
    RefDataService svc;
    svc.add_instrument(make_config(100));
    svc.add_instrument(make_config(101));
    svc.add_instrument(make_config(102));

    auto ids = svc.list_instruments();
    EXPECT_EQ(ids.size(), 3u);
}

TEST(RefDataServiceTest, ParticipantManagement) {
    RefDataService svc;
    RefDataService::ParticipantInfo info{};
    info.id = 1;
    info.name = "Test Broker";
    info.risk_limits.max_order_qty = 500;

    svc.add_participant(info);

    auto p = svc.get_participant(1);
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->name, "Test Broker");
    EXPECT_EQ(p->risk_limits.max_order_qty, 500u);
}

TEST(RefDataServiceTest, SessionConfiguration) {
    RefDataService svc;
    RefDataService::SessionConfig session{};
    session.session_id = 10;
    session.participant_id = 1;
    session.cod_enabled = true;
    session.rate_limit.max_messages_per_window = 500;

    svc.add_session(session);

    auto s = svc.get_session(10);
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->participant_id, 1u);
    EXPECT_TRUE(s->cod_enabled);
    EXPECT_EQ(s->rate_limit.max_messages_per_window, 500u);
}

TEST(RefDataServiceTest, PartitionAssignment) {
    RefDataService svc;
    svc.assign_to_partition(100, 1);
    svc.assign_to_partition(101, 2);

    EXPECT_EQ(svc.get_partition(100), 1);
    EXPECT_EQ(svc.get_partition(101), 2);
    EXPECT_EQ(svc.get_partition(999), 0);  // Unknown → 0
}

TEST(RefDataServiceTest, UpdateInstrument) {
    RefDataService svc;
    auto cfg = make_config(100);
    cfg.static_band_pct = 10.0;
    svc.add_instrument(cfg);

    cfg.static_band_pct = 25.0;
    svc.update_instrument(100, cfg);

    auto updated = svc.get_instrument(100);
    ASSERT_TRUE(updated.has_value());
    EXPECT_DOUBLE_EQ(updated->static_band_pct, 25.0);
}

TEST(RefDataServiceTest, Counts) {
    RefDataService svc;
    svc.add_instrument(make_config(100));
    svc.add_instrument(make_config(101));

    RefDataService::ParticipantInfo p{};
    p.id = 1; p.name = "P1";
    svc.add_participant(p);

    EXPECT_EQ(svc.instrument_count(), 2u);
    EXPECT_EQ(svc.participant_count(), 1u);
}

}  // namespace
}  // namespace refdata
