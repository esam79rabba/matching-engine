#include "ops/audit_log.h"
#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
#include <string>
#include <vector>

namespace ops {
namespace {

class AuditLogTest : public ::testing::Test {
protected:
    std::string test_path_;

    void SetUp() override {
        test_path_ = "/tmp/audit_log_test_" + std::to_string(getpid()) + ".jsonl";
    }

    void TearDown() override {
        std::remove(test_path_.c_str());
    }

    std::vector<std::string> read_lines() {
        std::vector<std::string> lines;
        std::ifstream in(test_path_);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty()) lines.push_back(line);
        }
        return lines;
    }
};

TEST_F(AuditLogTest, OpensFile) {
    AuditLog log(test_path_);
    EXPECT_TRUE(log.is_open());
}

TEST_F(AuditLogTest, WriteSingleEntry) {
    {
        AuditLog log(test_path_);
        log.log("admin", "halt", "instrument:100", "manual halt");
    }

    auto lines = read_lines();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_NE(lines[0].find("\"user\":\"admin\""), std::string::npos);
    EXPECT_NE(lines[0].find("\"action\":\"halt\""), std::string::npos);
    EXPECT_NE(lines[0].find("\"target\":\"instrument:100\""), std::string::npos);
    EXPECT_NE(lines[0].find("\"details\":\"manual halt\""), std::string::npos);
    EXPECT_NE(lines[0].find("\"timestamp\":"), std::string::npos);
}

TEST_F(AuditLogTest, WriteMultipleEntries) {
    {
        AuditLog log(test_path_);
        log.log("op1", "kill_switch_activate", "participant:5");
        log.log("op1", "kill_switch_deactivate", "participant:5");
        log.log("admin", "eod_trigger", "eod");
    }

    auto lines = read_lines();
    EXPECT_EQ(lines.size(), 3u);
}

TEST_F(AuditLogTest, EmptyDetailsField) {
    {
        AuditLog log(test_path_);
        log.log("viewer", "login", "auth");
    }

    auto lines = read_lines();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_NE(lines[0].find("\"details\":\"\""), std::string::npos);
}

TEST_F(AuditLogTest, EscapesQuotes) {
    {
        AuditLog log(test_path_);
        log.log("user\"name", "action", "target", "has \"quotes\"");
    }

    auto lines = read_lines();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_NE(lines[0].find("user\\\"name"), std::string::npos);
    EXPECT_NE(lines[0].find("has \\\"quotes\\\""), std::string::npos);
}

}  // namespace
}  // namespace ops
