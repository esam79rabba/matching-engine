#include "eod/eod_file_writer.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace eod {
namespace {

const std::string TEST_DIR = "/tmp/matching_engine_eod_test";

class EodFileWriterTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove_all(TEST_DIR);
    }
    void TearDown() override {
        std::filesystem::remove_all(TEST_DIR);
    }
};

EodReport make_test_report() {
    EodReport report;
    report.date = "2026-03-25";
    report.total_trades = 2;
    report.total_volume = 15;

    InstrumentSummary s{};
    s.instrument_id = 100;
    s.trade_count = 2;
    s.total_volume = 15;
    s.open_price = 50000;
    s.high_price = 52000;
    s.low_price = 50000;
    s.close_price = 52000;
    s.vwap_numerator = 50000 * 10 + 52000 * 5;
    report.instrument_summaries.push_back(s);

    ParticipantFees f1{};
    f1.participant_id = 1;
    f1.maker_volume = 10;
    f1.taker_volume = 5;
    f1.total_fees = 500;
    report.participant_fees.push_back(f1);

    return report;
}

TEST_F(EodFileWriterTest, WritesAllFiles) {
    auto report = make_test_report();
    EXPECT_TRUE(EodFileWriter::write_report(report, TEST_DIR));

    EXPECT_TRUE(std::filesystem::exists(TEST_DIR + "/eod_2026-03-25_summary.txt"));
    EXPECT_TRUE(std::filesystem::exists(TEST_DIR + "/eod_2026-03-25_trades.txt"));
    EXPECT_TRUE(std::filesystem::exists(TEST_DIR + "/eod_2026-03-25_fees.txt"));
}

TEST_F(EodFileWriterTest, TradeSummaryContainsCSV) {
    auto report = make_test_report();
    (void)EodFileWriter::write_report(report, TEST_DIR);

    std::ifstream f(TEST_DIR + "/eod_2026-03-25_trades.txt");
    std::stringstream buf;
    buf << f.rdbuf();
    std::string content = buf.str();

    EXPECT_NE(content.find("instrument_id,trade_count"), std::string::npos);
    EXPECT_NE(content.find("100,2,15,50000,52000,50000,52000"), std::string::npos);
}

TEST_F(EodFileWriterTest, FeeSummaryContainsCSV) {
    auto report = make_test_report();
    (void)EodFileWriter::write_report(report, TEST_DIR);

    std::ifstream f(TEST_DIR + "/eod_2026-03-25_fees.txt");
    std::stringstream buf;
    buf << f.rdbuf();
    std::string content = buf.str();

    EXPECT_NE(content.find("participant_id,maker_volume"), std::string::npos);
    EXPECT_NE(content.find("1,10,5,500"), std::string::npos);
}

TEST_F(EodFileWriterTest, EmptyReportWritesHeaders) {
    EodReport empty;
    empty.date = "2026-03-25";
    EXPECT_TRUE(EodFileWriter::write_report(empty, TEST_DIR));

    std::ifstream f(TEST_DIR + "/eod_2026-03-25_trades.txt");
    std::stringstream buf;
    buf << f.rdbuf();
    std::string content = buf.str();

    // Should have header but no data rows
    EXPECT_NE(content.find("instrument_id"), std::string::npos);
    EXPECT_EQ(content.find("100"), std::string::npos);  // No instrument data
}

TEST_F(EodFileWriterTest, SummaryContainsFullText) {
    auto report = make_test_report();
    (void)EodFileWriter::write_report(report, TEST_DIR);

    std::ifstream f(TEST_DIR + "/eod_2026-03-25_summary.txt");
    std::stringstream buf;
    buf << f.rdbuf();
    std::string content = buf.str();

    EXPECT_NE(content.find("END OF DAY REPORT"), std::string::npos);
    EXPECT_NE(content.find("Total trades: 2"), std::string::npos);
}

}  // namespace
}  // namespace eod
