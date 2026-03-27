#include "certification/cert_report_writer.h"

#include <fstream>
#include <ctime>
#include <sstream>

namespace certification {

// ─── JSON escaping helper ────────────────────────────────────────

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

// ─── CSV escaping helper ─────────────────────────────────────────

static std::string csv_escape(const std::string& s) {
    // If field contains comma, quote, or newline, wrap in quotes and double any quotes.
    bool needs_quote = false;
    for (char c : s) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needs_quote = true;
            break;
        }
    }
    if (!needs_quote) return s;

    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += "\"";
    return out;
}

// ─── write_json ──────────────────────────────────────────────────

bool CertReportWriter::write_json(const CertificationReport& report, const std::string& path,
                                    const std::string& environment,
                                    const std::string& version) {
    std::ofstream out(path);
    if (!out.is_open()) return false;

    out << "{\n";
    out << "  \"environment\": \"" << json_escape(environment) << "\",\n";
    out << "  \"version\": \"" << json_escape(version) << "\",\n";
    out << "  \"timestamp\": \"" << json_escape(timestamp_now()) << "\",\n";
    out << "  \"total\": " << report.total << ",\n";
    out << "  \"passed\": " << report.passed << ",\n";
    out << "  \"failed\": " << report.failed << ",\n";
    out << "  \"scenarios\": [\n";

    for (size_t i = 0; i < report.details.size(); ++i) {
        const auto& d = report.details[i];
        out << "    {\n";
        out << "      \"scenario\": \"" << json_escape(d.scenario) << "\",\n";
        out << "      \"passed\": " << (d.passed ? "true" : "false") << ",\n";
        out << "      \"details\": \"" << json_escape(d.details) << "\"\n";
        out << "    }";
        if (i + 1 < report.details.size()) out << ",";
        out << "\n";
    }

    out << "  ]\n";
    out << "}\n";

    out.flush();
    return out.good();
}

// ─── write_csv ───────────────────────────────────────────────────

bool CertReportWriter::write_csv(const CertificationReport& report, const std::string& path) {
    std::ofstream out(path);
    if (!out.is_open()) return false;

    out << "scenario,passed,details\n";

    for (const auto& d : report.details) {
        out << csv_escape(d.scenario) << ","
            << (d.passed ? "true" : "false") << ","
            << csv_escape(d.details) << "\n";
    }

    out.flush();
    return out.good();
}

// ─── timestamp_now ───────────────────────────────────────────────

std::string CertReportWriter::timestamp_now() {
    std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return std::string(buf);
}

}  // namespace certification
