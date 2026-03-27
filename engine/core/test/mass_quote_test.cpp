#include "engine/matching.h"
#include <gtest/gtest.h>

namespace engine {
namespace {

InstrumentConfig make_config(InstrumentId id = 100) {
    InstrumentConfig cfg{};
    cfg.instrument_id = id;
    cfg.tick_size = 1;
    cfg.lot_size = 1;
    cfg.min_quantity = 1;
    cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000;
    cfg.static_band_pct = 20.0;
    cfg.dynamic_band_pct = 10.0;
    cfg.residual_policy = ResidualPolicy::Cancel;
    cfg.collar_ticks = 10;
    cfg.stp_mode = STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    return cfg;
}

TEST(MassQuoteTest, InsertsBothSides) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    auto result = engine.process_mass_quote(1, 1001, 1002, 9900, 50, 10100, 50);
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.bid_order_id, 1001u);
    EXPECT_EQ(result.ask_order_id, 1002u);
    EXPECT_EQ(engine.book().best_bid(), 9900);
    EXPECT_EQ(engine.book().best_ask(), 10100);
    EXPECT_NE(engine.book().find_order(1001), nullptr);
    EXPECT_NE(engine.book().find_order(1002), nullptr);
}

TEST(MassQuoteTest, ReplacesExisting) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    engine.process_mass_quote(1, 1001, 1002, 9900, 50, 10100, 50);
    auto r2 = engine.process_mass_quote(1, 2001, 2002, 9950, 100, 10050, 100);
    EXPECT_TRUE(r2.accepted);
    // Old orders cancelled
    EXPECT_EQ(engine.book().find_order(1001), nullptr);
    EXPECT_EQ(engine.book().find_order(1002), nullptr);
    // New orders present
    EXPECT_EQ(engine.book().best_bid(), 9950);
    EXPECT_EQ(engine.book().best_ask(), 10050);
}

TEST(MassQuoteTest, RejectsInvalidPrice) {
    OrderPool pool(100);
    auto cfg = make_config();
    cfg.tick_size = 5;
    MatchingEngine engine(cfg, pool);

    // Bad tick
    auto r1 = engine.process_mass_quote(1, 1001, 1002, 9903, 50, 10100, 50);
    EXPECT_FALSE(r1.accepted);
    EXPECT_EQ(r1.reject_code, RejectCode::InvalidTick);

    // Crossed quote
    auto r2 = engine.process_mass_quote(1, 2001, 2002, 10100, 50, 9900, 50);
    EXPECT_FALSE(r2.accepted);
}

TEST(MassQuoteTest, CancelAll) {
    OrderPool pool(100);
    auto cfg = make_config();
    MatchingEngine engine(cfg, pool);

    engine.process_mass_quote(1, 1001, 1002, 9900, 50, 10100, 50);
    engine.process_mass_quote(2, 2001, 2002, 9800, 100, 10200, 100);

    engine.cancel_participant_quotes(1);
    EXPECT_EQ(engine.book().find_order(1001), nullptr);
    EXPECT_EQ(engine.book().find_order(1002), nullptr);
    EXPECT_NE(engine.book().find_order(2001), nullptr);
    EXPECT_NE(engine.book().find_order(2002), nullptr);
}

}  // namespace
}  // namespace engine
