#include "certification/cert_report_writer.h"

#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <regex>

namespace {

using namespace certification;

static std::string temp_path(const std::string& name) {
    return std::filesystem::temp_directory_path().string() + "/cert_report_test_" + name;
}

static std::string read_file(const std::string& path) {
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void remove_file(const std::string& path) {
    std::filesystem::remove(path);
}

// ─── WriteJsonReport ─────────────────────────────────────────────

TEST(CertReportWriterTest, WriteJsonReport) {
    auto path = temp_path("report.json");
    remove_file(path);

    CertificationReport report;
    report.record(CertResult{true, "Logon", "Logon succeeded"});
    report.record(CertResult{false, "HaltedOrder", "Expected reject, got ack"});
    report.record(CertResult{true, "Cancel", "Cancel confirmed"});

    ASSERT_TRUE(CertReportWriter::write_json(report, path, "test-env", "v1.0"));

    auto content = read_file(path);

    // Verify key JSON strings are present.
    EXPECT_NE(content.find("\"environment\": \"test-env\""), std::string::npos);
    EXPECT_NE(content.find("\"version\": \"v1.0\""), std::string::npos);
    EXPECT_NE(content.find("\"timestamp\":"), std::string::npos);
    EXPECT_NE(content.find("\"total\": 3"), std::string::npos);
    EXPECT_NE(content.find("\"passed\": 2"), std::string::npos);
    EXPECT_NE(content.find("\"failed\": 1"), std::string::npos);
    EXPECT_NE(content.find("\"scenario\": \"Logon\""), std::string::npos);
    EXPECT_NE(content.find("\"scenario\": \"HaltedOrder\""), std::string::npos);
    EXPECT_NE(content.find("\"passed\": true"), std::string::npos);
    EXPECT_NE(content.find("\"passed\": false"), std::string::npos);

    remove_file(path);
}

// ─── WriteCsvReport ──────────────────────────────────────────────

TEST(CertReportWriterTest, WriteCsvReport) {
    auto path = temp_path("report.csv");
    remove_file(path);

    CertificationReport report;
    report.record(CertResult{true, "Logon", "OK"});
    report.record(CertResult{false, "Reject", "Missing field"});

    ASSERT_TRUE(CertReportWriter::write_csv(report, path));

    auto content = read_file(path);

    // Verify header.
    EXPECT_NE(content.find("scenario,passed,details"), std::string::npos);
    // Verify rows.
    EXPECT_NE(content.find("Logon,true,OK"), std::string::npos);
    EXPECT_NE(content.find("Reject,false,Missing field"), std::string::npos);

    remove_file(path);
}

// ─── TimestampFormat ─────────────────────────────────────────────

TEST(CertReportWriterTest, TimestampFormat) {
    auto ts = CertReportWriter::timestamp_now();

    // ISO 8601 pattern: YYYY-MM-DDTHH:MM:SSZ
    std::regex iso_pattern(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z)");
    EXPECT_TRUE(std::regex_match(ts, iso_pattern))
        << "Timestamp '" << ts << "' does not match ISO 8601 pattern";
}

}  // namespace
