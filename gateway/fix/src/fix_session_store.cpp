#include "gateway/fix_session_store.h"

#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdio>
#include <filesystem>

namespace gateway {

FixSessionStore::FixSessionStore(const std::string& path)
    : path_(path) {}

bool FixSessionStore::load() {
    sessions_.clear();

    std::ifstream in(path_);
    if (!in.is_open()) {
        // File doesn't exist — fresh start, not an error.
        return true;
    }

    std::string line;
    int line_num = 0;
    while (std::getline(in, line)) {
        ++line_num;
        if (line.empty()) continue;

        // Parse: session_id|sender_comp_id|target_comp_id|inbound_seq|outbound_seq
        std::istringstream iss(line);
        std::string token;
        std::vector<std::string> tokens;
        while (std::getline(iss, token, '|')) {
            tokens.push_back(token);
        }

        if (tokens.size() != 5) {
            std::cerr << "[FixSessionStore] Skipping malformed line " << line_num
                      << ": " << line << "\n";
            continue;
        }

        try {
            PersistedSessionState state;
            state.session_id     = static_cast<engine::SessionId>(std::stoul(tokens[0]));
            state.sender_comp_id = tokens[1];
            state.target_comp_id = tokens[2];
            state.inbound_seq    = std::stoull(tokens[3]);
            state.outbound_seq   = std::stoull(tokens[4]);

            sessions_[state.sender_comp_id] = state;
        } catch (const std::exception& e) {
            std::cerr << "[FixSessionStore] Skipping malformed line " << line_num
                      << ": " << e.what() << "\n";
            continue;
        }
    }

    return true;
}

bool FixSessionStore::save() const {
    // Atomic write: write to temp file, then rename.
    const std::string tmp_path = path_ + ".tmp";

    std::ofstream out(tmp_path);
    if (!out.is_open()) {
        std::cerr << "[FixSessionStore] Failed to open temp file for writing: " << tmp_path << "\n";
        return false;
    }

    for (const auto& [key, state] : sessions_) {
        out << state.session_id << "|"
            << state.sender_comp_id << "|"
            << state.target_comp_id << "|"
            << state.inbound_seq << "|"
            << state.outbound_seq << "\n";
    }

    out.flush();
    if (!out.good()) {
        std::cerr << "[FixSessionStore] Write error on temp file: " << tmp_path << "\n";
        return false;
    }
    out.close();

    // Rename temp → final (atomic on most filesystems)
    std::error_code ec;
    std::filesystem::rename(tmp_path, path_, ec);
    if (ec) {
        std::cerr << "[FixSessionStore] Failed to rename temp file: " << ec.message() << "\n";
        return false;
    }

    return true;
}

std::optional<PersistedSessionState> FixSessionStore::get(const std::string& sender_comp_id) const {
    auto it = sessions_.find(sender_comp_id);
    if (it == sessions_.end()) return std::nullopt;
    return it->second;
}

void FixSessionStore::upsert(const PersistedSessionState& state) {
    sessions_[state.sender_comp_id] = state;
}

void FixSessionStore::remove(const std::string& sender_comp_id) {
    sessions_.erase(sender_comp_id);
}

}  // namespace gateway
