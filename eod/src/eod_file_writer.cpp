#include "eod/eod_file_writer.h"
#include <fstream>
#include <sstream>
#include <filesystem>

namespace eod {

bool EodFileWriter::write_report(const EodReport& report, const std::string& output_dir) {
    std::filesystem::create_directories(output_dir);

    std::string summary_path = output_dir + "/eod_" + report.date + "_summary.txt";
    std::string trades_path = output_dir + "/eod_" + report.date + "_trades.txt";
    std::string fees_path = output_dir + "/eod_" + report.date + "_fees.txt";

    // Write full summary
    {
        std::ofstream f(summary_path);
        if (!f.is_open()) return false;
        f << report.to_text();
    }

    if (!write_trade_summary(report, trades_path)) return false;
    if (!write_fee_summary(report, fees_path)) return false;

    return true;
}

bool EodFileWriter::write_trade_summary(const EodReport& report, const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) return false;

    f << "instrument_id,trade_count,total_volume,open,high,low,close,vwap\n";
    for (const auto& s : report.instrument_summaries) {
        int64_t vwap = (s.total_volume > 0)
            ? (s.vwap_numerator / static_cast<int64_t>(s.total_volume))
            : 0;
        f << s.instrument_id << ","
          << s.trade_count << ","
          << s.total_volume << ","
          << s.open_price << ","
          << s.high_price << ","
          << s.low_price << ","
          << s.close_price << ","
          << vwap << "\n";
    }

    return true;
}

bool EodFileWriter::write_fee_summary(const EodReport& report, const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) return false;

    f << "participant_id,maker_volume,taker_volume,total_fees\n";
    for (const auto& fee : report.participant_fees) {
        f << fee.participant_id << ","
          << fee.maker_volume << ","
          << fee.taker_volume << ","
          << fee.total_fees << "\n";
    }

    return true;
}

}  // namespace eod
