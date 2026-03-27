#include "engine/session_scheduler.h"

namespace engine {

SessionScheduler::SessionScheduler(ITimeSource& clock) : clock_(clock) {}

void SessionScheduler::add_profile(const SessionProfile& profile) {
    profiles_[profile.instrument_id] = profile;
    progress_[profile.instrument_id] = 0;
}

void SessionScheduler::register_engine(InstrumentId id, MatchingEngine* engine) {
    if (engine) engines_[id] = engine;
}

uint32_t SessionScheduler::tick() {
    Timestamp now = clock_.now();
    uint32_t applied = 0;

    for (auto& [inst_id, profile] : profiles_) {
        auto prog_it = progress_.find(inst_id);
        if (prog_it == progress_.end()) continue;
        size_t& idx = prog_it->second;

        auto eng_it = engines_.find(inst_id);
        if (eng_it == engines_.end()) continue;
        MatchingEngine* engine = eng_it->second;

        // Apply all transitions that are due (may skip multiple if tick is late)
        while (idx < profile.transitions.size()) {
            const auto& trans = profile.transitions[idx];
            if (now < trans.trigger_time) break;  // Not yet due

            // Attempt transition. If the current state has been manually
            // overridden (e.g., halted by operator), the transition may
            // be invalid — in that case we skip it gracefully.
            bool ok = engine->transition_state(trans.target_state);
            if (ok) ++applied;
            // Advance regardless — don't retry failed transitions
            ++idx;
        }
    }

    return applied;
}

Timestamp SessionScheduler::next_transition_time(InstrumentId id) const {
    auto prof_it = profiles_.find(id);
    auto prog_it = progress_.find(id);
    if (prof_it == profiles_.end() || prog_it == progress_.end()) return 0;

    size_t idx = prog_it->second;
    if (idx >= prof_it->second.transitions.size()) return 0;  // All done
    return prof_it->second.transitions[idx].trigger_time;
}

void SessionScheduler::reset() {
    for (auto& [id, idx] : progress_) {
        idx = 0;
    }
}

}  // namespace engine
