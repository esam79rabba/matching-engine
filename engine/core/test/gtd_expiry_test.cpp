#include "engine/gtd_expiry_checker.h"
#include "engine/journal_stub.h"
#include <gtest/gtest.h>

namespace engine {
namespace {

constexpr Timestamp SEC = 1000000000ULL;

InstrumentConfig make_config(InstrumentId id = 100) {
    InstrumentConfig cfg{};
    cfg.instrument_id = id; cfg.tick_size = 1; cfg.lot_size = 1;
    cfg.min_quantity = 1; cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000; cfg.static_band_pct = 20.0;
    cfg.residual_policy = ResidualPolicy::Cancel;
    cfg.stp_mode = STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    return cfg;
}

PartitionMessage make_gtd_order(Price price, Timestamp expiry, ClientOrderId clord = 1) {
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::NewOrder;
    msg.timestamp = 1000; msg.client_order_id = clord;
    msg.instrument_id = 100; msg.side = Side::Buy;
    msg.order_type = OrderType::Limit; msg.time_in_force = TimeInForce::GTD;
    msg.price = price; msg.quantity = 50;
    msg.participant_id = 1; msg.account_id = 1; msg.session_id = 1;
    msg.expiry_time = expiry;
    return msg;
}

PartitionMessage make_gtc_order(Price price, ClientOrderId clord = 1) {
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::NewOrder;
    msg.timestamp = 1000; msg.client_order_id = clord;
    msg.instrument_id = 100; msg.side = Side::Buy;
    msg.order_type = OrderType::Limit; msg.time_in_force = TimeInForce::GTC;
    msg.price = price; msg.quantity = 50;
    msg.participant_id = 1; msg.account_id = 1; msg.session_id = 1;
    return msg;
}

TEST(GtdExpiryTest, ExpiredGTDCancelledOnTick) {
    ManualTimeSource clock(0);
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);
    part.add_instrument(make_config());

    GtdExpiryChecker checker(clock);
    checker.register_partition(&part);

    // GTD expiring at 10 seconds
    (void)part.process(make_gtd_order(9999, 10 * SEC, 1));
    EXPECT_EQ(part.get_engine(100)->book().order_count(), 1u);

    // Before expiry
    clock.set(5 * SEC);
    EXPECT_EQ(checker.tick(), 0u);
    EXPECT_EQ(part.get_engine(100)->book().order_count(), 1u);

    // At expiry
    clock.set(10 * SEC);
    EXPECT_EQ(checker.tick(), 1u);
    EXPECT_EQ(part.get_engine(100)->book().order_count(), 0u);
}

TEST(GtdExpiryTest, NonExpiredGTDRetained) {
    ManualTimeSource clock(0);
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);
    part.add_instrument(make_config());

    GtdExpiryChecker checker(clock);
    checker.register_partition(&part);

    // GTD expiring at 100 seconds
    (void)part.process(make_gtd_order(9999, 100 * SEC, 1));

    clock.set(50 * SEC);
    EXPECT_EQ(checker.tick(), 0u);
    EXPECT_EQ(part.get_engine(100)->book().order_count(), 1u);
}

TEST(GtdExpiryTest, GTCNotAffected) {
    ManualTimeSource clock(0);
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);
    part.add_instrument(make_config());

    GtdExpiryChecker checker(clock);
    checker.register_partition(&part);

    (void)part.process(make_gtd_order(9999, 10 * SEC, 1));  // Expires at 10s
    (void)part.process(make_gtc_order(9998, 2));              // GTC — no expiry
    EXPECT_EQ(part.get_engine(100)->book().order_count(), 2u);

    clock.set(15 * SEC);
    EXPECT_EQ(checker.tick(), 1u);  // Only GTD cancelled
    EXPECT_EQ(part.get_engine(100)->book().order_count(), 1u);
}

TEST(GtdExpiryTest, IdempotentRepeatedTick) {
    ManualTimeSource clock(0);
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);
    part.add_instrument(make_config());

    GtdExpiryChecker checker(clock);
    checker.register_partition(&part);

    (void)part.process(make_gtd_order(9999, 10 * SEC, 1));
    clock.set(15 * SEC);

    EXPECT_EQ(checker.tick(), 1u);
    EXPECT_EQ(checker.tick(), 0u);  // Already cancelled — no-op
}

TEST(GtdExpiryTest, MultiplePartitions) {
    ManualTimeSource clock(0);
    JournalStub j1(10000), j2(10000);
    Partition part1(1, 10000, &j1);
    Partition part2(2, 10000, &j2);
    part1.add_instrument(make_config(100));
    part2.add_instrument(make_config(200));

    GtdExpiryChecker checker(clock);
    checker.register_partition(&part1);
    checker.register_partition(&part2);

    // GTD on both partitions
    PartitionMessage msg1 = make_gtd_order(9999, 10 * SEC, 1);
    msg1.instrument_id = 100;
    (void)part1.process(msg1);

    PartitionMessage msg2 = make_gtd_order(9998, 10 * SEC, 2);
    msg2.instrument_id = 200;
    (void)part2.process(msg2);

    clock.set(10 * SEC);
    EXPECT_EQ(checker.tick(), 2u);  // Both cancelled
    EXPECT_EQ(part1.get_engine(100)->book().order_count(), 0u);
    EXPECT_EQ(part2.get_engine(200)->book().order_count(), 0u);
}

}  // namespace
}  // namespace engine
