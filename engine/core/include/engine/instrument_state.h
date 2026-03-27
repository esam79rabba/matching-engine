#pragma once

#include "engine/types.h"
#include <cstdint>

namespace engine {

/// Instrument trading state machine.
///
/// Enforces valid state transitions and defines which order actions
/// are permitted in each state. Used by the MatchingEngine to gate
/// order acceptance.
///
/// States:
///   INACTIVE    — not scheduled for trading
///   PRE_OPEN    — orders accepted, no matching
///   CONTINUOUS  — normal trading (matching active)
///   HALTED      — trading halted (cancel-only)
///   CLOSED      — session ended (no orders accepted)
///
/// Auction states (AUCTION, CLOSING_AUCTION) are placeholders.
/// Full auction logic is stretch / Phase 2.
///
/// Generic — no asset-class-specific behavior.
class InstrumentStateMachine {
public:
    InstrumentStateMachine();

    /// Attempt a state transition. Returns true if the transition is valid.
    [[nodiscard]] bool transition(InstrumentState new_state);

    /// Current state.
    [[nodiscard]] InstrumentState state() const noexcept { return state_; }

    // ─── State-aware permission queries ─────────────────────

    /// Can new orders be submitted in the current state?
    [[nodiscard]] bool accepts_new_orders() const noexcept;

    /// Can orders be cancelled in the current state?
    [[nodiscard]] bool accepts_cancels() const noexcept;

    /// Can orders be modified in the current state?
    [[nodiscard]] bool accepts_modifies() const noexcept;

    /// Is matching active in the current state?
    [[nodiscard]] bool matching_active() const noexcept;

    /// Force state (for snapshot restore — bypasses transition validation).
    void force_state(InstrumentState s) noexcept { state_ = s; }

    // ─── Transition validation ──────────────────────────────

    /// Check if a transition from current state to new_state is valid.
    [[nodiscard]] static bool is_valid_transition(InstrumentState from, InstrumentState to);

private:
    InstrumentState state_ = InstrumentState::Inactive;
};

}  // namespace engine
