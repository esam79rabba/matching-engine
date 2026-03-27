#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <fstream>

namespace replication {

/// Monotonic generation counter for split-brain fencing.
///
/// Each promotion increments the generation. A site with a lower generation
/// than the current promoted site is considered stale and must not accept
/// new orders or claim to be primary.
///
/// Persisted to file so it survives restarts.
class GenerationCounter {
public:
    explicit GenerationCounter(const std::string& path = "");

    /// Get current generation.
    uint64_t current() const { return generation_.load(); }

    /// Increment and persist. Returns the new generation.
    uint64_t increment();

    /// Check if a given generation is current (not stale).
    bool is_current(uint64_t gen) const { return gen == generation_.load(); }

    /// Check if a given generation is stale (lower than current).
    bool is_stale(uint64_t gen) const { return gen < generation_.load(); }

    /// Load from file. Returns current generation.
    uint64_t load();

    /// Persist to file.
    bool save() const;

private:
    std::atomic<uint64_t> generation_{0};
    std::string path_;
};

}  // namespace replication
