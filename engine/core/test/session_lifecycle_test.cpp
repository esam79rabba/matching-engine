#include "engine/session_lifecycle.h"
#include "engine/journal_stub.h"
#include <gtest/gtest.h>

namespace engine {
namespace {

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

PartitionMessage make_order(Side side, Price price, Quantity qty,
                             TimeInForce tif = TimeInForce::Day,
                             ClientOrderId clord = 1, Timestamp expiry = 0) {
    PartitionMessage msg{};
    msg.type = PartitionMessage::Type::NewOrder;
    msg.timestamp = 1000; msg.client_order_id = clord;
    msg.instrument_id = 100; msg.side = side;
    msg.order_type = OrderType::Limit; msg.time_in_force = tif;
    msg.price = price; msg.quantity = qty;
    msg.participant_id = 1; msg.account_id = 1; msg.session_id = 1;
    msg.expiry_time = expiry;
    return msg;
}

TEST(SessionLifecycleTest, DayOrdersCancelledOnClose) {
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);
    part.add_instrument(make_config());

    // Insert 3 Day orders
    (void)part.process(make_order(Side::Buy, 9999, 50, TimeInForce::Day, 1));
    (void)part.process(make_order(Side::Buy, 9998, 50, TimeInForce::Day, 2));
    (void)part.process(make_order(Side::Sell, 10050, 30, TimeInForce::Day, 3));
    EXPECT_EQ(part.get_engine(100)->book().order_count(), 3u);

    uint32_t cancelled = SessionLifecycle::on_session_close(part, 100, 5000);
    EXPECT_EQ(cancelled, 3u);
    EXPECT_EQ(part.get_engine(100)->book().order_count(), 0u);
}

TEST(SessionLifecycleTest, GTCOrdersSurviveClose) {
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);
    part.add_instrument(make_config());

    // Mix of Day and GTC
    (void)part.process(make_order(Side::Buy, 9999, 50, TimeInForce::Day, 1));
    (void)part.process(make_order(Side::Buy, 9998, 50, TimeInForce::GTC, 2));
    (void)part.process(make_order(Side::Sell, 10050, 30, TimeInForce::GTC, 3));
    EXPECT_EQ(part.get_engine(100)->book().order_count(), 3u);

    uint32_t cancelled = SessionLifecycle::on_session_close(part, 100, 5000);
    EXPECT_EQ(cancelled, 1u);  // Only the Day order
    EXPECT_EQ(part.get_engine(100)->book().order_count(), 2u);
}

TEST(SessionLifecycleTest, GTDExpiredOrderCancelledOnClose) {
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);
    part.add_instrument(make_config());

    // GTD order expiring at time=3000
    (void)part.process(make_order(Side::Buy, 9999, 50, TimeInForce::GTD, 1, 3000));
    // GTD order expiring at time=10000 (not yet expired)
    (void)part.process(make_order(Side::Buy, 9998, 50, TimeInForce::GTD, 2, 10000));
    EXPECT_EQ(part.get_engine(100)->book().order_count(), 2u);

    // Close at time=5000 — first GTD expired, second not
    uint32_t cancelled = SessionLifecycle::on_session_close(part, 100, 5000);
    EXPECT_EQ(cancelled, 1u);
    EXPECT_EQ(part.get_engine(100)->book().order_count(), 1u);
}

TEST(SessionLifecycleTest, EmptyBookNoOp) {
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);
    part.add_instrument(make_config());

    uint32_t cancelled = SessionLifecycle::on_session_close(part, 100, 5000);
    EXPECT_EQ(cancelled, 0u);
}

TEST(SessionLifecycleTest, NonExistentInstrumentNoOp) {
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);
    part.add_instrument(make_config());

    uint32_t cancelled = SessionLifecycle::on_session_close(part, 999, 5000);
    EXPECT_EQ(cancelled, 0u);
}

TEST(SessionLifecycleTest, IdempotentOnRepeatedClose) {
    JournalStub journal(10000);
    Partition part(1, 10000, &journal);
    part.add_instrument(make_config());

    (void)part.process(make_order(Side::Buy, 9999, 50, TimeInForce::Day, 1));
    (void)SessionLifecycle::on_session_close(part, 100, 5000);
    EXPECT_EQ(part.get_engine(100)->book().order_count(), 0u);

    // Second close — nothing to cancel
    uint32_t cancelled = SessionLifecycle::on_session_close(part, 100, 6000);
    EXPECT_EQ(cancelled, 0u);
}

}  // namespace
}  // namespace engine
