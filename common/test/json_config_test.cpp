#include "config/json_config_loader.h"

#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
#include <string>

namespace {

/// Write content to a temporary file and return its path.
std::string write_tmp_file(const std::string& name, const std::string& content) {
    std::string path = "/tmp/" + name;
    std::ofstream out(path);
    out << content;
    out.close();
    return path;
}

}  // namespace

TEST(JsonConfigTest, LoadInstruments) {
    const char* json = R"({
  "instruments": [
    {
      "instrument_id": 100,
      "symbol": "BTC-USD",
      "description": "Bitcoin / US Dollar Spot",
      "tick_size": 1,
      "lot_size": 1,
      "min_quantity": 1,
      "max_quantity": 1000000,
      "static_ref_price": 50000,
      "static_band_pct": 10.0,
      "dynamic_band_pct": 10.0,
      "residual_policy": "cancel",
      "stp_mode": "cancel_newest",
      "partition_id": 1,
      "collar_ticks": 10
    },
    {
      "instrument_id": 101,
      "symbol": "ETH-USD",
      "description": "Ethereum / US Dollar Spot",
      "tick_size": 1,
      "lot_size": 1,
      "min_quantity": 1,
      "max_quantity": 10000000,
      "static_ref_price": 3000,
      "static_band_pct": 10.0,
      "residual_policy": "cancel",
      "stp_mode": "cancel_newest",
      "partition_id": 1
    }
  ]
})";

    auto path = write_tmp_file("test_instruments.json", json);
    auto result = config::JsonConfigLoader::load_instruments(path);

    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(result.instruments.size(), 2u);

    // Verify BTC-USD
    const auto& btc = result.instruments[0];
    EXPECT_EQ(btc.instrument_id, 100u);
    EXPECT_EQ(btc.symbol, "BTC-USD");
    EXPECT_EQ(btc.description, "Bitcoin / US Dollar Spot");
    EXPECT_EQ(btc.partition_id, 1u);
    EXPECT_EQ(btc.config.instrument_id, 100u);
    EXPECT_EQ(btc.config.tick_size, 1);
    EXPECT_EQ(btc.config.lot_size, 1u);
    EXPECT_EQ(btc.config.min_quantity, 1u);
    EXPECT_EQ(btc.config.max_quantity, 1000000u);
    EXPECT_EQ(btc.config.static_ref_price, 50000);
    EXPECT_DOUBLE_EQ(btc.config.static_band_pct, 10.0);
    EXPECT_EQ(btc.config.residual_policy, engine::ResidualPolicy::Cancel);
    EXPECT_EQ(btc.config.stp_mode, engine::STPMode::CancelNewest);
    EXPECT_EQ(btc.config.collar_ticks, 10u);

    // Verify ETH-USD
    const auto& eth = result.instruments[1];
    EXPECT_EQ(eth.instrument_id, 101u);
    EXPECT_EQ(eth.symbol, "ETH-USD");
    EXPECT_EQ(eth.config.max_quantity, 10000000u);
    EXPECT_EQ(eth.config.static_ref_price, 3000);

    std::remove(path.c_str());
}

TEST(JsonConfigTest, LoadSessions) {
    const char* json = R"({
  "session_profile": {
    "name": "default_continuous",
    "transitions": [
      { "state": "PRE_OPEN",   "time": "08:00:00" },
      { "state": "CONTINUOUS", "time": "09:00:00" },
      { "state": "CLOSED",    "time": "17:00:00" }
    ],
    "timezone": "Asia/Dubai"
  },
  "participants": [
    {
      "participant_id": 1,
      "name": "Market Maker Alpha",
      "sender_comp_id": "MM_ALPHA",
      "max_order_rate": 1000,
      "fat_finger_price_pct": 10.0,
      "fat_finger_max_notional": 10000000
    },
    {
      "participant_id": 2,
      "name": "Broker Beta",
      "sender_comp_id": "BRK_BETA",
      "max_order_rate": 500,
      "fat_finger_price_pct": 5.0,
      "fat_finger_max_notional": 5000000
    }
  ]
})";

    auto path = write_tmp_file("test_sessions.json", json);
    auto result = config::JsonConfigLoader::load_sessions(path);

    ASSERT_TRUE(result.success) << result.error;
    ASSERT_TRUE(result.session.has_value());

    const auto& session = *result.session;
    EXPECT_EQ(session.name, "default_continuous");
    EXPECT_EQ(session.timezone, "Asia/Dubai");
    ASSERT_EQ(session.transitions.size(), 3u);
    EXPECT_EQ(session.transitions[0].state, "PRE_OPEN");
    EXPECT_EQ(session.transitions[0].time, "08:00:00");
    EXPECT_EQ(session.transitions[1].state, "CONTINUOUS");
    EXPECT_EQ(session.transitions[2].state, "CLOSED");

    ASSERT_EQ(session.participants.size(), 2u);
    EXPECT_EQ(session.participants[0].participant_id, 1u);
    EXPECT_EQ(session.participants[0].name, "Market Maker Alpha");
    EXPECT_EQ(session.participants[0].sender_comp_id, "MM_ALPHA");
    EXPECT_EQ(session.participants[0].max_order_rate, 1000u);
    EXPECT_DOUBLE_EQ(session.participants[0].fat_finger_price_pct, 10.0);
    EXPECT_DOUBLE_EQ(session.participants[0].fat_finger_max_notional, 10000000.0);

    EXPECT_EQ(session.participants[1].participant_id, 2u);
    EXPECT_EQ(session.participants[1].name, "Broker Beta");
    EXPECT_EQ(session.participants[1].sender_comp_id, "BRK_BETA");
    EXPECT_EQ(session.participants[1].max_order_rate, 500u);

    std::remove(path.c_str());
}

TEST(JsonConfigTest, MissingFileReturnsError) {
    auto result = config::JsonConfigLoader::load_instruments("/tmp/nonexistent_file_xyz.json");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST(JsonConfigTest, InvalidJsonReturnsError) {
    auto path = write_tmp_file("test_invalid.json", "{ this is not valid json !!! }}}");
    auto result = config::JsonConfigLoader::load_instruments(path);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
    std::remove(path.c_str());
}

TEST(JsonConfigTest, EmptyInstrumentsArray) {
    const char* json = R"({ "instruments": [] })";
    auto path = write_tmp_file("test_empty_instruments.json", json);
    auto result = config::JsonConfigLoader::load_instruments(path);

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.instruments.size(), 0u);

    std::remove(path.c_str());
}
