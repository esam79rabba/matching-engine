#include "dropcopy/drop_copy_service.h"
#include <gtest/gtest.h>

namespace dropcopy {
namespace {

TEST(DropCopyTest, RoutesFillToCorrectParticipant) {
    DropCopyService svc;
    TestDropCopySink sink1, sink2;
    svc.register_sink(1, &sink1);
    svc.register_sink(2, &sink2);

    svc.on_order_fill(100, 1001, 50, 1, engine::Side::Buy, 50000, 10, 10, 0, 1000);

    ASSERT_EQ(sink1.events.size(), 1u);
    EXPECT_EQ(sink1.events[0].type, DropCopyEvent::Type::OrderFill);
    EXPECT_EQ(sink1.events[0].fill_price, 50000);
    EXPECT_EQ(sink2.events.size(), 0u);  // Participant 2 gets nothing
}

TEST(DropCopyTest, RoutesNewOrderToParticipant) {
    DropCopyService svc;
    TestDropCopySink sink;
    svc.register_sink(1, &sink);

    svc.on_order_new(100, 1001, 50, 1, engine::Side::Buy, 2000);

    ASSERT_EQ(sink.events.size(), 1u);
    EXPECT_EQ(sink.events[0].type, DropCopyEvent::Type::OrderNew);
    EXPECT_EQ(sink.events[0].order_id, 100u);
}

TEST(DropCopyTest, RoutesCancelToParticipant) {
    DropCopyService svc;
    TestDropCopySink sink;
    svc.register_sink(1, &sink);

    svc.on_order_cancel(100, 1001, 50, 1, engine::Side::Sell, 3000);

    ASSERT_EQ(sink.events.size(), 1u);
    EXPECT_EQ(sink.events[0].type, DropCopyEvent::Type::OrderCancel);
}

TEST(DropCopyTest, RoutesRejectToParticipant) {
    DropCopyService svc;
    TestDropCopySink sink;
    svc.register_sink(1, &sink);

    svc.on_order_reject(1001, 50, 1, engine::RejectCode::PriceOutsideBands, 4000);

    ASSERT_EQ(sink.events.size(), 1u);
    EXPECT_EQ(sink.events[0].type, DropCopyEvent::Type::OrderReject);
    EXPECT_EQ(sink.events[0].reject_code, engine::RejectCode::PriceOutsideBands);
}

TEST(DropCopyTest, SequenceMonotonicallyIncreases) {
    DropCopyService svc;
    TestDropCopySink sink;
    svc.register_sink(1, &sink);

    svc.on_order_new(100, 1, 50, 1, engine::Side::Buy, 1000);
    svc.on_order_fill(100, 1, 50, 1, engine::Side::Buy, 50000, 10, 10, 0, 2000);
    svc.on_order_cancel(101, 2, 50, 1, engine::Side::Sell, 3000);

    ASSERT_EQ(sink.events.size(), 3u);
    EXPECT_EQ(sink.events[0].sequence, 1u);
    EXPECT_EQ(sink.events[1].sequence, 2u);
    EXPECT_EQ(sink.events[2].sequence, 3u);
}

TEST(DropCopyTest, UnregisteredParticipantDropsSilently) {
    DropCopyService svc;
    // No sink registered for participant 99
    svc.on_order_new(100, 1, 50, 99, engine::Side::Buy, 1000);
    EXPECT_EQ(svc.sequence(), 1u);  // Sequence still increments
}

TEST(DropCopyTest, UnregisterRemovesSink) {
    DropCopyService svc;
    TestDropCopySink sink;
    svc.register_sink(1, &sink);
    svc.on_order_new(100, 1, 50, 1, engine::Side::Buy, 1000);
    EXPECT_EQ(sink.events.size(), 1u);

    svc.unregister_sink(1);
    svc.on_order_new(101, 2, 50, 1, engine::Side::Sell, 2000);
    EXPECT_EQ(sink.events.size(), 1u);  // No new events after unregister
}

}  // namespace
}  // namespace dropcopy
