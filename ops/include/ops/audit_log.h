#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <mutex>

namespace ops {

class AuditLog {
public:
    explicit AuditLog(const std::string& path);
    ~AuditLog();

    void log(const std::string& user, const std::string& action,
             const std::string& target, const std::string& details = "");

    /// Read the most recent N entries as a JSON array string.
    std::string read_recent(uint32_t limit = 50) const;

    bool is_open() const { return file_.is_open(); }
    const std::string& path() const { return path_; }

private:
    std::string path_;
    std::ofstream file_;
    mutable std::mutex mutex_;
    static std::string timestamp_now();
};

}  // namespace ops
