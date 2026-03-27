#pragma once
#include "certification/cert_harness.h"
#include <string>

namespace certification {

/// Writes certification reports to files.
/// Generates both JSON and CSV formats.
class CertReportWriter {
public:
    /// Write report as JSON to the given path.
    static bool write_json(const CertificationReport& report, const std::string& path,
                           const std::string& environment = "prototype",
                           const std::string& version = "FR-1");

    /// Write report as CSV to the given path.
    static bool write_csv(const CertificationReport& report, const std::string& path);

    /// Generate a timestamp string (UTC ISO 8601).
    static std::string timestamp_now();
};

}  // namespace certification
