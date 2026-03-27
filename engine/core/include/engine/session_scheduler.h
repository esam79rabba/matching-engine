#pragma once

#include "engine/types.h"
#include "engine/matching.h"
#include <vector>
#include <unordered_map>
#include <functional>

namespace engine {

/// Abstract time source for testability.
/// Production: PTP clock. Tests: manual advance.
class ITimeSource {
public:
    virtual ~ITimeSource() = default;
    [[nodiscard]] virtual Timestamp now() const noexcept = 0;
};

/// Manual time source for deterministic testing.
class ManualTimeSource : public ITimeSource {
public:
    explicit ManualTimeSource(Timestamp initial = 0) : t_(initial) {}
    [[nodiscard]] Timestamp now() const noexcept override { return t_; }
    void set(Timestamp t) noexcept { t_ = t; }
    void advance(Timestamp delta) noexcept { t_ += delta; }
private:
    Timestamp t_;
};

/// A scheduled phase transition for an instrument.
struct ScheduledTransition {
    Timestamp        trigger_time;
    InstrumentState  target_state;
};

/// Session profile: ordered list of transitions for an instrument's trading day.
struct SessionProfile {
    InstrumentId                    instrument_id;
    std::vector<ScheduledTransition> transitions;  // Must be sorted by trigger_time
};

/// Session scheduler — drives automatic instrument state transitions.
///
/// Polls on tick() calls. Each tick checks the time source and applies
/// any due transitions. This is NOT on the hot path — it runs on the
/// control plane at a configurable frequency (e.g., every 100ms).
///
/// Manual ops overrides remain possible: the scheduler checks the
/// current state before transitioning. If an operator has already
/// moved the instrument to a different state (e.g., halted), the
/// scheduled transition is skipped if it would be an invalid transition.
///
/// Generic — no asset-class-specific logic.
class SessionScheduler {
public:
    explicit SessionScheduler(ITimeSource& clock);

    /// Register a session profile for an instrument.
    void add_profile(const SessionProfile& profile);

    /// Register a mutable engine reference for state transitions.
    void register_engine(InstrumentId id, MatchingEngine* engine);

    /// Tick: check for due transitions and apply them.
    /// Returns the number of transitions applied.
    uint32_t tick();

    /// Query: next scheduled transition time for an instrument.
    [[nodiscard]] Timestamp next_transition_time(InstrumentId id) const;

    /// Reset all progress (e.g., for next trading day).
    void reset();

private:
    ITimeSource& clock_;
    std::unordered_map<InstrumentId, SessionProfile> profiles_;
    std::unordered_map<InstrumentId, MatchingEngine*> engines_;
    std::unordered_map<InstrumentId, size_t> progress_;  // Index into transitions vector
};

}  // namespace engine
