#include "engine/instrument_lifecycle.h"
#include "engine/journal_stub.h"
#include <gtest/gtest.h>

namespace engine {
namespace {

InstrumentConfig make_config(InstrumentId id = 200) {
    InstrumentConfig cfg{};
    cfg.instrument_id = id; cfg.tick_size = 1; cfg.lot_size = 1;
    cfg.min_quantity = 1; cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000; cfg.static_band_pct = 20.0;
    cfg.residual_policy = ResidualPolicy::Cancel;
    cfg.stp_mode = STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    return cfg;
}

PartitionMessage make_order(InstrumentId inst, Side side, Price price,
                             Quantity qty, ClientOrderId clord = 1) {
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::NewOrder;
    msg.timestamp = 1000; msg.client_order_id = clord;
    msg.instrument_id = inst; msg.side = side;
    msg.order_type = OrderType::Limit; msg.time_in_force = TimeInForce::Day;
    msg.price = price; msg.quantity = qty;
    msg.participant_id = 1; msg.account_id = 1; msg.session_id = 1;
    return msg;
}

TEST(InstrumentLifecycleTest, ListNewInstrument) {
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);

    auto cfg = make_config(200);
    EXPECT_TRUE(InstrumentLifecycle::list_instrument(part, cfg));
    EXPECT_TRUE(InstrumentLifecycle::instrument_exists(part, 200));

    // Starts in Inactive — orders should be rejected
    auto result = part.process(make_order(200, Side::Buy, 10000, 50));
    EXPECT_EQ(result.action, MatchResult::Action::Rejected);
}

TEST(InstrumentLifecycleTest, DuplicateListRejected) {
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);

    auto cfg = make_config(200);
    EXPECT_TRUE(InstrumentLifecycle::list_instrument(part, cfg));
    EXPECT_FALSE(InstrumentLifecycle::list_instrument(part, cfg));  // Duplicate
}

TEST(InstrumentLifecycleTest, ActivateEnablesTrading) {
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);

    auto cfg = make_config(200);
    (void)InstrumentLifecycle::list_instrument(part, cfg);
    EXPECT_TRUE(InstrumentLifecycle::activate_instrument(part, 200));

    // Now in Continuous — orders should be accepted
    auto result = part.process(make_order(200, Side::Buy, 10000, 50));
    EXPECT_EQ(result.action, MatchResult::Action::Inserted);
}

TEST(InstrumentLifecycleTest, ActivateNonExistentFails) {
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);

    EXPECT_FALSE(InstrumentLifecycle::activate_instrument(part, 999));
}

TEST(InstrumentLifecycleTest, DelistCancelsOrders) {
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);

    auto cfg = make_config(200);
    (void)InstrumentLifecycle::list_instrument(part, cfg);
    (void)InstrumentLifecycle::activate_instrument(part, 200);

    // Insert some orders
    (void)part.process(make_order(200, Side::Buy, 9999, 50, 1));
    (void)part.process(make_order(200, Side::Sell, 10050, 30, 2));
    EXPECT_EQ(part.get_engine(200)->book().order_count(), 2u);

    // Delist
    uint32_t cancelled = InstrumentLifecycle::delist_instrument(part, 200, 5000);
    EXPECT_EQ(cancelled, 2u);
    EXPECT_EQ(part.get_engine(200)->book().order_count(), 0u);
    EXPECT_EQ(part.get_engine(200)->state(), InstrumentState::Closed);
}

TEST(InstrumentLifecycleTest, DelistNonExistentReturnsZero) {
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);

    EXPECT_EQ(InstrumentLifecycle::delist_instrument(part, 999, 5000), 0u);
}

TEST(InstrumentLifecycleTest, OrderRejectedAfterDelist) {
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);

    auto cfg = make_config(200);
    (void)InstrumentLifecycle::list_instrument(part, cfg);
    (void)InstrumentLifecycle::activate_instrument(part, 200);
    (void)InstrumentLifecycle::delist_instrument(part, 200, 5000);

    // New order should be rejected (state = Closed)
    auto result = part.process(make_order(200, Side::Buy, 10000, 50, 3));
    EXPECT_EQ(result.action, MatchResult::Action::Rejected);
}

}  // namespace
}  // namespace engine
