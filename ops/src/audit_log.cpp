#include "ops/audit_log.h"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace ops {

AuditLog::AuditLog(const std::string& path) : path_(path) {
    file_.open(path, std::ios::app);
}

AuditLog::~AuditLog() {
    if (file_.is_open()) file_.close();
}

void AuditLog::log(const std::string& user, const std::string& action,
                   const std::string& target, const std::string& details) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_.is_open()) return;

    // Escape double quotes in strings for JSON safety
    auto escape = [](const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else out += c;
        }
        return out;
    };

    file_ << "{\"timestamp\":\"" << timestamp_now()
          << "\",\"user\":\"" << escape(user)
          << "\",\"action\":\"" << escape(action)
          << "\",\"target\":\"" << escape(target)
          << "\",\"details\":\"" << escape(details)
          << "\"}\n";
    file_.flush();
}

std::string AuditLog::read_recent(uint32_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    // Read all lines from the file, return last N as JSON array
    std::ifstream in(path_);
    if (!in.is_open()) return "[]";

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) lines.push_back(line);
    }

    // Take last `limit` entries, reversed (newest first)
    std::string result = "[";
    uint32_t count = 0;
    for (auto it = lines.rbegin(); it != lines.rend() && count < limit; ++it, ++count) {
        if (count > 0) result += ",";
        result += *it;  // Already JSON
    }
    result += "]";
    return result;
}

std::string AuditLog::timestamp_now() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_r(&time_t_now, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

}  // namespace ops
