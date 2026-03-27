#include "replication/generation_counter.h"

#include <fstream>
#include <string>

namespace replication {

GenerationCounter::GenerationCounter(const std::string& path)
    : path_(path) {
    if (!path_.empty()) {
        load();
    }
}

uint64_t GenerationCounter::increment() {
    uint64_t new_gen = generation_.fetch_add(1) + 1;
    if (!path_.empty()) {
        save();
    }
    return new_gen;
}

uint64_t GenerationCounter::load() {
    if (path_.empty()) {
        return generation_.load();
    }

    std::ifstream in(path_);
    if (!in.is_open()) {
        // File doesn't exist — start at 0
        generation_.store(0);
        return 0;
    }

    uint64_t val = 0;
    if (in >> val) {
        generation_.store(val);
    } else {
        generation_.store(0);
    }
    return generation_.load();
}

bool GenerationCounter::save() const {
    if (path_.empty()) {
        return false;
    }

    std::ofstream out(path_, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out << generation_.load();
    out.flush();
    return out.good();
}

}  // namespace replication
