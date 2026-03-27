#include "engine/session_scheduler.h"
#include <gtest/gtest.h>

namespace engine {
namespace {

constexpr Timestamp SEC = 1000000000ULL;  // 1 second in nanos

InstrumentConfig make_config(InstrumentId id = 100) {
    InstrumentConfig cfg{};
    cfg.instrument_id = id; cfg.tick_size = 1; cfg.lot_size = 1;
    cfg.min_quantity = 1; cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000; cfg.static_band_pct = 20.0;
    cfg.dynamic_band_pct = 10.0; cfg.residual_policy = ResidualPolicy::Cancel;
    cfg.collar_ticks = 10; cfg.stp_mode = STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    return cfg;
}

TEST(SessionSchedulerTest, TransitionsAtCorrectTime) {
    ManualTimeSource clock(0);
    SessionScheduler scheduler(clock);

    OrderPool pool(1000);
    auto cfg = make_config(100);
    MatchingEngine engine(cfg, pool);
    // Engine starts in Continuous by default — force to Inactive for session test
    engine.force_state(InstrumentState::Inactive);

    SessionProfile profile;
    profile.instrument_id = 100;
    profile.transitions = {
        {10 * SEC, InstrumentState::PreOpen},
        {20 * SEC, InstrumentState::Continuous},
        {30 * SEC, InstrumentState::Closed},
    };

    scheduler.add_profile(profile);
    scheduler.register_engine(100, &engine);

    // Before first transition
    clock.set(5 * SEC);
    EXPECT_EQ(scheduler.tick(), 0u);
    EXPECT_EQ(engine.state(), InstrumentState::Inactive);

    // At PRE_OPEN time
    clock.set(10 * SEC);
    EXPECT_EQ(scheduler.tick(), 1u);
    EXPECT_EQ(engine.state(), InstrumentState::PreOpen);

    // Between PRE_OPEN and CONTINUOUS
    clock.set(15 * SEC);
    EXPECT_EQ(scheduler.tick(), 0u);
    EXPECT_EQ(engine.state(), InstrumentState::PreOpen);

    // At CONTINUOUS time
    clock.set(20 * SEC);
    EXPECT_EQ(scheduler.tick(), 1u);
    EXPECT_EQ(engine.state(), InstrumentState::Continuous);

    // At CLOSED time
    clock.set(30 * SEC);
    EXPECT_EQ(scheduler.tick(), 1u);
    EXPECT_EQ(engine.state(), InstrumentState::Closed);

    // After all transitions — no more
    clock.set(40 * SEC);
    EXPECT_EQ(scheduler.tick(), 0u);
}

TEST(SessionSchedulerTest, MultipleInstruments) {
    ManualTimeSource clock(0);
    SessionScheduler scheduler(clock);

    OrderPool pool(2000);
    auto cfg1 = make_config(100);
    auto cfg2 = make_config(101);
    MatchingEngine eng1(cfg1, pool);
    MatchingEngine eng2(cfg2, pool);
    eng1.force_state(InstrumentState::Inactive);
    eng2.force_state(InstrumentState::Inactive);

    // Different schedules
    scheduler.add_profile({100, {{10 * SEC, InstrumentState::PreOpen}, {20 * SEC, InstrumentState::Continuous}}});
    scheduler.add_profile({101, {{15 * SEC, InstrumentState::PreOpen}, {25 * SEC, InstrumentState::Continuous}}});
    scheduler.register_engine(100, &eng1);
    scheduler.register_engine(101, &eng2);

    clock.set(12 * SEC);
    EXPECT_EQ(scheduler.tick(), 1u);  // Only inst 100
    EXPECT_EQ(eng1.state(), InstrumentState::PreOpen);
    EXPECT_EQ(eng2.state(), InstrumentState::Inactive);

    clock.set(16 * SEC);
    EXPECT_EQ(scheduler.tick(), 1u);  // Only inst 101
    EXPECT_EQ(eng2.state(), InstrumentState::PreOpen);
}

TEST(SessionSchedulerTest, ManualOverrideSkipsScheduledTransition) {
    ManualTimeSource clock(0);
    SessionScheduler scheduler(clock);

    OrderPool pool(1000);
    auto cfg = make_config(100);
    MatchingEngine engine(cfg, pool);
    // Start in Continuous
    engine.force_state(InstrumentState::Continuous);
    // Operator halts the instrument manually
    (void)engine.transition_state(InstrumentState::Halted);

    // Schedule says go to Closed at t=10, but Halted→Closed is not valid
    scheduler.add_profile({100, {{10 * SEC, InstrumentState::Closed}}});
    scheduler.register_engine(100, &engine);

    clock.set(10 * SEC);
    uint32_t applied = scheduler.tick();
    EXPECT_EQ(applied, 0u);  // Transition skipped (invalid from Halted)
    EXPECT_EQ(engine.state(), InstrumentState::Halted);  // Unchanged
}

TEST(SessionSchedulerTest, NextTransitionTime) {
    ManualTimeSource clock(0);
    SessionScheduler scheduler(clock);

    scheduler.add_profile({100, {{10 * SEC, InstrumentState::PreOpen}, {20 * SEC, InstrumentState::Continuous}}});

    EXPECT_EQ(scheduler.next_transition_time(100), 10 * SEC);

    // After first transition
    OrderPool pool(1000);
    auto cfg = make_config(100);
    MatchingEngine engine(cfg, pool);
    engine.force_state(InstrumentState::Inactive);
    scheduler.register_engine(100, &engine);

    clock.set(10 * SEC);
    (void)scheduler.tick();
    EXPECT_EQ(scheduler.next_transition_time(100), 20 * SEC);
}

TEST(SessionSchedulerTest, ResetRestartsCycle) {
    ManualTimeSource clock(0);
    SessionScheduler scheduler(clock);

    OrderPool pool(1000);
    auto cfg = make_config(100);
    MatchingEngine engine(cfg, pool);
    engine.force_state(InstrumentState::Inactive);

    scheduler.add_profile({100, {{10 * SEC, InstrumentState::PreOpen}}});
    scheduler.register_engine(100, &engine);

    clock.set(10 * SEC);
    (void)scheduler.tick();
    EXPECT_EQ(engine.state(), InstrumentState::PreOpen);

    // Reset and replay
    engine.force_state(InstrumentState::Inactive);
    scheduler.reset();
    clock.set(10 * SEC);
    EXPECT_EQ(scheduler.tick(), 1u);
    EXPECT_EQ(engine.state(), InstrumentState::PreOpen);
}

}  // namespace
}  // namespace engine
