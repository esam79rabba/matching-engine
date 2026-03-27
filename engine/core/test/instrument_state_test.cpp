#include "engine/instrument_state.h"
#include <gtest/gtest.h>

namespace engine {
namespace {

TEST(InstrumentStateTest, StartsInactive) {
    InstrumentStateMachine sm;
    EXPECT_EQ(sm.state(), InstrumentState::Inactive);
}

TEST(InstrumentStateTest, ValidTransitionInactiveToPreOpen) {
    InstrumentStateMachine sm;
    EXPECT_TRUE(sm.transition(InstrumentState::PreOpen));
    EXPECT_EQ(sm.state(), InstrumentState::PreOpen);
}

TEST(InstrumentStateTest, ValidTransitionPreOpenToContinuous) {
    InstrumentStateMachine sm;
    (void)sm.transition(InstrumentState::PreOpen);
    EXPECT_TRUE(sm.transition(InstrumentState::Continuous));
    EXPECT_EQ(sm.state(), InstrumentState::Continuous);
}

TEST(InstrumentStateTest, InvalidTransitionInactiveToContinuous) {
    InstrumentStateMachine sm;
    EXPECT_FALSE(sm.transition(InstrumentState::Continuous));
    EXPECT_EQ(sm.state(), InstrumentState::Inactive);  // Unchanged
}

TEST(InstrumentStateTest, HaltFromContinuous) {
    InstrumentStateMachine sm;
    (void)sm.transition(InstrumentState::PreOpen);
    (void)sm.transition(InstrumentState::Continuous);
    EXPECT_TRUE(sm.transition(InstrumentState::Halted));
    EXPECT_EQ(sm.state(), InstrumentState::Halted);
}

TEST(InstrumentStateTest, ResumeFromHalt) {
    InstrumentStateMachine sm;
    (void)sm.transition(InstrumentState::PreOpen);
    (void)sm.transition(InstrumentState::Continuous);
    (void)sm.transition(InstrumentState::Halted);
    EXPECT_TRUE(sm.transition(InstrumentState::Continuous));
    EXPECT_EQ(sm.state(), InstrumentState::Continuous);
}

TEST(InstrumentStateTest, CloseFromContinuous) {
    InstrumentStateMachine sm;
    (void)sm.transition(InstrumentState::PreOpen);
    (void)sm.transition(InstrumentState::Continuous);
    EXPECT_TRUE(sm.transition(InstrumentState::Closed));
    EXPECT_EQ(sm.state(), InstrumentState::Closed);
}

TEST(InstrumentStateTest, PermissionsInContinuous) {
    InstrumentStateMachine sm;
    (void)sm.transition(InstrumentState::PreOpen);
    (void)sm.transition(InstrumentState::Continuous);

    EXPECT_TRUE(sm.accepts_new_orders());
    EXPECT_TRUE(sm.accepts_cancels());
    EXPECT_TRUE(sm.accepts_modifies());
    EXPECT_TRUE(sm.matching_active());
}

TEST(InstrumentStateTest, PermissionsInHalted) {
    InstrumentStateMachine sm;
    (void)sm.transition(InstrumentState::PreOpen);
    (void)sm.transition(InstrumentState::Continuous);
    (void)sm.transition(InstrumentState::Halted);

    EXPECT_FALSE(sm.accepts_new_orders());
    EXPECT_TRUE(sm.accepts_cancels());     // Can cancel during halt
    EXPECT_FALSE(sm.accepts_modifies());
    EXPECT_FALSE(sm.matching_active());
}

TEST(InstrumentStateTest, PermissionsInPreOpen) {
    InstrumentStateMachine sm;
    (void)sm.transition(InstrumentState::PreOpen);

    EXPECT_TRUE(sm.accepts_new_orders());
    EXPECT_TRUE(sm.accepts_cancels());
    EXPECT_TRUE(sm.accepts_modifies());
    EXPECT_FALSE(sm.matching_active());    // No matching in pre-open
}

TEST(InstrumentStateTest, PermissionsInClosed) {
    InstrumentStateMachine sm;
    (void)sm.transition(InstrumentState::PreOpen);
    (void)sm.transition(InstrumentState::Continuous);
    (void)sm.transition(InstrumentState::Closed);

    EXPECT_FALSE(sm.accepts_new_orders());
    EXPECT_FALSE(sm.accepts_cancels());
    EXPECT_FALSE(sm.accepts_modifies());
    EXPECT_FALSE(sm.matching_active());
}

TEST(InstrumentStateTest, ForceStateBypassesValidation) {
    InstrumentStateMachine sm;
    sm.force_state(InstrumentState::Continuous);
    EXPECT_EQ(sm.state(), InstrumentState::Continuous);
}

TEST(InstrumentStateTest, ClosedToPreOpenForNextSession) {
    InstrumentStateMachine sm;
    sm.force_state(InstrumentState::Closed);
    EXPECT_TRUE(sm.transition(InstrumentState::PreOpen));
}

}  // namespace
}  // namespace engine
