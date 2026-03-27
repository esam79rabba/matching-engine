#pragma once

#include "eod/eod_processor.h"
#include <string>

namespace eod {

/// EOD file writer — writes report output to disk.
///
/// File naming convention:
///   {output_dir}/eod_{date}_trades.txt
///   {output_dir}/eod_{date}_fees.txt
///   {output_dir}/eod_{date}_summary.txt
///
/// Format: plain text for prototype. Production: CSV or structured binary.
///
/// Generic — no asset-class-specific logic.
class EodFileWriter {
public:
    /// Write the full EOD report to the specified output directory.
    /// Creates the directory if it doesn't exist.
    /// Returns true if all files written successfully.
    static bool write_report(const EodReport& report, const std::string& output_dir);

    /// Write trade summary only.
    static bool write_trade_summary(const EodReport& report, const std::string& path);

    /// Write fee summary only.
    static bool write_fee_summary(const EodReport& report, const std::string& path);
};

}  // namespace eod
