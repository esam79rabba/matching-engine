#include "engine/partition.h"
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

PartitionMessage make_new_order(InstrumentId inst, Side side, Price price, Quantity qty,
                                 OrderType type = OrderType::Limit) {
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::NewOrder;
    msg.sequence = 0;  // Partition assigns internally
    msg.timestamp = 1000000000;  // 1 second in nanoseconds
    msg.client_order_id = 1;
    msg.instrument_id = inst;
    msg.side = side;
    msg.order_type = type;
    msg.time_in_force = TimeInForce::Day;
    msg.price = price;
    msg.quantity = qty;
    msg.participant_id = 1;
    msg.account_id = 1;
    msg.session_id = 1;
    msg.stp_group_id = 0;
    msg.flags = 0;
    return msg;
}

TEST(PartitionTest, CreatePartitionWithInstrument) {
    Partition part(1, 1000);
    part.add_instrument(make_config(100));
    part.add_instrument(make_config(101));

    EXPECT_EQ(part.id(), 1);
    EXPECT_EQ(part.instrument_count(), 2u);
    EXPECT_EQ(part.sequence(), 0u);
}

TEST(PartitionTest, ProcessNewOrderAdvancesSequence) {
    Partition part(1, 1000);
    part.add_instrument(make_config(100));

    auto msg = make_new_order(100, Side::Buy, 10000, 50);
    auto result = part.process(msg);

    EXPECT_EQ(result.action, MatchResult::Action::Inserted);
    EXPECT_EQ(part.sequence(), 1u);

    // Second message advances to 2
    auto msg2 = make_new_order(100, Side::Sell, 10050, 50);
    part.process(msg2);
    EXPECT_EQ(part.sequence(), 2u);
}

TEST(PartitionTest, UnknownInstrumentRejected) {
    Partition part(1, 1000);
    part.add_instrument(make_config(100));

    auto msg = make_new_order(999, Side::Buy, 10000, 50);
    auto result = part.process(msg);

    EXPECT_EQ(result.action, MatchResult::Action::Rejected);
    EXPECT_EQ(result.reject_code, RejectCode::UnknownInstrument);
}

TEST(PartitionTest, MatchAcrossOrdersOnSameInstrument) {
    Partition part(1, 1000);
    part.add_instrument(make_config(100));

    // Insert resting sell
    auto sell = make_new_order(100, Side::Sell, 10000, 50);
    part.process(sell);

    // Aggressive buy — should match
    auto buy = make_new_order(100, Side::Buy, 10000, 50);
    auto result = part.process(buy);

    EXPECT_EQ(result.action, MatchResult::Action::Filled);
}

TEST(PartitionTest, MultipleInstrumentsIndependent) {
    Partition part(1, 1000);
    part.add_instrument(make_config(100));
    part.add_instrument(make_config(101));

    // Order on instrument 100
    auto msg1 = make_new_order(100, Side::Buy, 10000, 50);
    part.process(msg1);

    // Order on instrument 101 (price within 20% band of ref=10000)
    auto msg2 = make_new_order(101, Side::Sell, 11000, 30);
    part.process(msg2);

    // Verify independence — orders on different instruments don't interact
    const auto* eng100 = part.get_engine(100);
    const auto* eng101 = part.get_engine(101);
    ASSERT_NE(eng100, nullptr);
    ASSERT_NE(eng101, nullptr);
    EXPECT_EQ(eng100->book().best_bid(), 10000);
    EXPECT_EQ(eng100->book().best_ask(), 0);      // No sell on 100
    EXPECT_EQ(eng101->book().best_ask(), 11000);
    EXPECT_EQ(eng101->book().best_bid(), 0);       // No buy on 101
}

TEST(PartitionTest, OrderIdEncodesPartitionId) {
    Partition part(5, 1000);
    part.add_instrument(make_config(100));

    auto msg = make_new_order(100, Side::Buy, 10000, 50);
    part.process(msg);

    const auto* eng = part.get_engine(100);
    ASSERT_NE(eng, nullptr);

    Order* order = eng->book().find_order(
        (static_cast<uint64_t>(5) << 48) | 1  // partition_id=5, sequence=1
    );
    EXPECT_NE(order, nullptr);
}

}  // namespace
}  // namespace engine
