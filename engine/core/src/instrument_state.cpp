#include "engine/instrument_state.h"

namespace engine {

InstrumentStateMachine::InstrumentStateMachine()
    : state_(InstrumentState::Inactive) {}

bool InstrumentStateMachine::transition(InstrumentState new_state) {
    if (!is_valid_transition(state_, new_state)) return false;
    state_ = new_state;
    return true;
}

bool InstrumentStateMachine::accepts_new_orders() const noexcept {
    switch (state_) {
        case InstrumentState::PreOpen:
        case InstrumentState::Continuous:
        case InstrumentState::Auction:         // Phase 2: auction orders
        case InstrumentState::ClosingAuction:  // Phase 2
            return true;
        default:
            return false;
    }
}

bool InstrumentStateMachine::accepts_cancels() const noexcept {
    switch (state_) {
        case InstrumentState::PreOpen:
        case InstrumentState::Continuous:
        case InstrumentState::Auction:
        case InstrumentState::ClosingAuction:
        case InstrumentState::Halted:      // Can cancel during halt
        case InstrumentState::Suspended:   // Can cancel during suspend
            return true;
        default:
            return false;
    }
}

bool InstrumentStateMachine::accepts_modifies() const noexcept {
    switch (state_) {
        case InstrumentState::PreOpen:
        case InstrumentState::Continuous:
        case InstrumentState::Auction:
        case InstrumentState::ClosingAuction:
            return true;
        default:
            return false;
    }
}

bool InstrumentStateMachine::matching_active() const noexcept {
    return state_ == InstrumentState::Continuous;
    // Phase 2: Auction end also triggers matching (uncrossing).
}

bool InstrumentStateMachine::is_valid_transition(InstrumentState from, InstrumentState to) {
    // Valid transition table based on frozen baseline (§09 state machine)
    switch (from) {
        case InstrumentState::Inactive:
            return to == InstrumentState::PreOpen;

        case InstrumentState::PreOpen:
            return to == InstrumentState::Continuous ||
                   to == InstrumentState::Auction;     // Phase 2: opening auction

        case InstrumentState::Auction:
            return to == InstrumentState::Continuous || // Auction → Continuous
                   to == InstrumentState::Halted;       // Auction → Halt

        case InstrumentState::Continuous:
            return to == InstrumentState::ClosingAuction ||
                   to == InstrumentState::Halted ||
                   to == InstrumentState::Suspended ||
                   to == InstrumentState::Closed ||
                   to == InstrumentState::Auction;     // Volatility auction

        case InstrumentState::ClosingAuction:
            return to == InstrumentState::Closed ||
                   to == InstrumentState::PostClose;

        case InstrumentState::Halted:
            return to == InstrumentState::Continuous ||       // Resume
                   to == InstrumentState::Auction ||          // Resume via auction
                   to == InstrumentState::ClosingAuction ||   // Resume into closing auction
                   to == InstrumentState::Suspended;          // Escalate

        case InstrumentState::Suspended:
            return to == InstrumentState::Continuous || // Resume
                   to == InstrumentState::PreOpen;      // Re-open

        case InstrumentState::PostClose:
            return to == InstrumentState::Closed;

        case InstrumentState::Closed:
            return to == InstrumentState::Inactive ||  // End of day
                   to == InstrumentState::PreOpen;     // Next session

        default:
            return false;
    }
}

}  // namespace engine
