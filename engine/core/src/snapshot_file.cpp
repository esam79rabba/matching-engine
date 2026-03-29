#include "engine/snapshot_file.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>

namespace engine {

// ─── Constants ──────────────────────────────────────────────────

static constexpr char SNAPSHOT_MAGIC[8] = {'M','E','S','N','A','P','0','1'};
static constexpr uint32_t SNAPSHOT_VERSION = 1;

// ─── Binary write helpers (little-endian native on x86/ARM) ─────

static void write_u8(std::ofstream& out, uint8_t v)   { out.write(reinterpret_cast<const char*>(&v), 1); }
static void write_u16(std::ofstream& out, uint16_t v)  { out.write(reinterpret_cast<const char*>(&v), 2); }
static void write_u32(std::ofstream& out, uint32_t v)  { out.write(reinterpret_cast<const char*>(&v), 4); }
static void write_u64(std::ofstream& out, uint64_t v)  { out.write(reinterpret_cast<const char*>(&v), 8); }
static void write_i64(std::ofstream& out, int64_t v)   { out.write(reinterpret_cast<const char*>(&v), 8); }

// ─── Binary read helpers ────────────────────────────────────────

static bool read_u8(std::ifstream& in, uint8_t& v)   { in.read(reinterpret_cast<char*>(&v), 1); return in.good(); }
static bool read_u16(std::ifstream& in, uint16_t& v)  { in.read(reinterpret_cast<char*>(&v), 2); return in.good(); }
static bool read_u32(std::ifstream& in, uint32_t& v)  { in.read(reinterpret_cast<char*>(&v), 4); return in.good(); }
static bool read_u64(std::ifstream& in, uint64_t& v)  { in.read(reinterpret_cast<char*>(&v), 8); return in.good(); }
static bool read_i64(std::ifstream& in, int64_t& v)   { in.read(reinterpret_cast<char*>(&v), 8); return in.good(); }

// ─── Write an order field by field ──────────────────────────────

static void write_order(std::ofstream& out, const Order& o) {
    write_u32(out, o.instrument_id);
    write_u64(out, o.order_id);
    write_u64(out, o.client_order_id);
    write_u32(out, o.participant_id);
    write_u32(out, o.account_id);
    write_u32(out, o.session_id);
    write_u8(out, static_cast<uint8_t>(o.side));
    write_u8(out, static_cast<uint8_t>(o.order_type));
    write_u8(out, static_cast<uint8_t>(o.time_in_force));
    write_i64(out, o.price);
    write_u64(out, o.original_qty);
    write_u64(out, o.remaining_qty);
    write_u64(out, o.display_qty);
    write_u64(out, o.cumulative_filled);
    write_i64(out, o.stop_price);
    write_i64(out, o.trail_offset);
    write_u8(out, static_cast<uint8_t>(o.peg_type));
    write_i64(out, o.peg_offset);
    write_i64(out, o.peg_limit);
    write_i64(out, o.budget);
    write_i64(out, o.budget_spent);
    write_u32(out, o.stp_group_id);
    write_u16(out, o.flags);
    write_u64(out, o.entry_sequence);
    write_u64(out, o.entry_timestamp);
    write_u64(out, o.expiry_time);
}

// ─── Read an order field by field ───────────────────────────────

static bool read_order(std::ifstream& in, SnapshotFileReader::OrderData& od) {
    uint8_t u8val;

    if (!read_u32(in, od.instrument_id)) return false;
    if (!read_u64(in, od.order_id)) return false;
    if (!read_u64(in, od.client_order_id)) return false;
    if (!read_u32(in, od.participant_id)) return false;
    if (!read_u32(in, od.account_id)) return false;
    if (!read_u32(in, od.session_id)) return false;

    if (!read_u8(in, u8val)) return false;
    od.side = static_cast<Side>(u8val);
    if (!read_u8(in, u8val)) return false;
    od.order_type = static_cast<OrderType>(u8val);
    if (!read_u8(in, u8val)) return false;
    od.time_in_force = static_cast<TimeInForce>(u8val);

    if (!read_i64(in, od.price)) return false;
    if (!read_u64(in, od.original_qty)) return false;
    if (!read_u64(in, od.remaining_qty)) return false;
    if (!read_u64(in, od.display_qty)) return false;
    if (!read_u64(in, od.cumulative_filled)) return false;
    if (!read_i64(in, od.stop_price)) return false;
    if (!read_i64(in, od.trail_offset)) return false;

    if (!read_u8(in, u8val)) return false;
    od.peg_type = static_cast<PegType>(u8val);

    if (!read_i64(in, od.peg_offset)) return false;
    if (!read_i64(in, od.peg_limit)) return false;
    if (!read_i64(in, od.budget)) return false;
    if (!read_i64(in, od.budget_spent)) return false;
    if (!read_u32(in, od.stp_group_id)) return false;
    if (!read_u16(in, od.flags)) return false;
    if (!read_u64(in, od.entry_sequence)) return false;
    if (!read_u64(in, od.entry_timestamp)) return false;
    if (!read_u64(in, od.expiry_time)) return false;

    return true;
}

// ═══════════════════════════════════════════════════════════════
// SnapshotFileWriter
// ═══════════════════════════════════════════════════════════════

bool SnapshotFileWriter::save(
    const std::string& path,
    const std::unordered_map<PartitionId, std::unique_ptr<Partition>>& partitions) {

    // Write to a temp file first, then atomically rename
    std::string tmp_path = path + ".tmp";

    // Ensure parent directory exists
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;

    // Count total orders across all partitions
    uint64_t total_orders = 0;
    for (const auto& [pid, part] : partitions) {
        for (const auto& [inst_id, eng] : part->engines()) {
            total_orders += eng.book().order_count();
        }
    }

    // Timestamp: current time in nanoseconds
    auto now = std::chrono::system_clock::now();
    auto ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count());

    // ── Header ──
    out.write(SNAPSHOT_MAGIC, 8);
    write_u32(out, SNAPSHOT_VERSION);
    write_u32(out, static_cast<uint32_t>(partitions.size()));
    write_u64(out, total_orders);
    write_u64(out, ns);

    // ── Per-partition data ──
    for (const auto& [pid, part] : partitions) {
        // Count orders in this partition
        uint32_t part_order_count = 0;
        for (const auto& [inst_id, eng] : part->engines()) {
            part_order_count += eng.book().order_count();
        }

        write_u16(out, pid);
        write_u32(out, part_order_count);
        write_u64(out, part->snapshot_sequence());

        // Write each order field by field
        for (const auto& [inst_id, eng] : part->engines()) {
            eng.book().for_each_order([&](const Order& order) {
                write_order(out, order);
            });
        }
    }

    out.flush();
    if (!out.good()) {
        out.close();
        std::remove(tmp_path.c_str());
        return false;
    }
    out.close();

    // Atomic rename: tmp → final
    std::error_code ec;
    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
        std::remove(tmp_path.c_str());
        return false;
    }

    return true;
}

// ═══════════════════════════════════════════════════════════════
// SnapshotFileReader
// ═══════════════════════════════════════════════════════════════

std::optional<SnapshotFileReader::SnapshotData> SnapshotFileReader::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return std::nullopt;

    // ── Verify magic ──
    char magic[8];
    in.read(magic, 8);
    if (!in.good() || std::memcmp(magic, SNAPSHOT_MAGIC, 8) != 0) {
        return std::nullopt;
    }

    SnapshotData data{};

    // ── Read header ──
    if (!read_u32(in, data.version)) return std::nullopt;
    if (data.version != SNAPSHOT_VERSION) return std::nullopt;

    uint32_t partition_count = 0;
    uint64_t total_orders = 0;
    if (!read_u32(in, partition_count)) return std::nullopt;
    if (!read_u64(in, total_orders)) return std::nullopt;
    if (!read_u64(in, data.timestamp)) return std::nullopt;

    // ── Per-partition data ──
    data.partitions.reserve(partition_count);
    uint64_t orders_read = 0;

    for (uint32_t p = 0; p < partition_count; ++p) {
        PartitionData pd{};
        uint32_t order_count = 0;

        if (!read_u16(in, pd.partition_id)) return std::nullopt;
        if (!read_u32(in, order_count)) return std::nullopt;
        if (!read_u64(in, pd.sequence)) return std::nullopt;

        pd.orders.reserve(order_count);
        for (uint32_t i = 0; i < order_count; ++i) {
            OrderData od{};
            if (!read_order(in, od)) return std::nullopt;
            pd.orders.push_back(od);
            ++orders_read;
        }

        data.partitions.push_back(std::move(pd));
    }

    // Sanity check: total orders should match header
    if (orders_read != total_orders) return std::nullopt;

    return data;
}

}  // namespace engine
