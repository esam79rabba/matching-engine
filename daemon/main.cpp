/// Matching Engine Daemon — Milestone 12 standalone service entry point.
///
/// Wires together the full runtime stack:
///   RefData → Risk → Journal → Partition → MatchingEngine
///   FIX TCP Acceptor → RealFixAdapter → Partition
///   Partition → MarketDataPublisher → MdTcpServer + MdUdpPublisher
///   Partition → DepthPublisher + StatisticsTracker
///   OpsController + RefDataHttpHandler → HttpServer
///
/// Usage: matching-engine-daemon [config-path]
///   Default config: ../deploy/config/platform.conf

#include "engine/partition.h"
#include "engine/matching.h"
#include "engine/trade_event_bus.h"
#include "engine/journal_segmented.h"
#include "engine/journal_direct_io.h"
#include "refdata/ref_data_service.h"
#include "refdata/ref_data_http.h"
#include "risk/risk_service.h"
#include "gateway/fix_parser.h"
#include "gateway/fix_message_store_file.h"
#include "gateway/refdata_gateway_bridge.h"
#include "gateway/tcp/fix_tcp_acceptor.h"
#include "transport/socket.h"
#include "marketdata/publisher.h"
#include "marketdata/transport/md_tcp_server.h"
#include "marketdata/transport/md_udp_publisher.h"
#include "marketdata/depth_publisher.h"
#include "marketdata/statistics.h"
#include "ops/ops_controller.h"
#include "ops/http/http_server.h"
#include "ops/auth_middleware.h"
#include "ops/auth_service.h"
#include "ops/audit_log.h"
#include "ops/trade_audit_trail.h"
#include "posttrade/trade_store.h"
#include "surveillance/surveillance_engine.h"
#include "surveillance/rules/wash_trade_rule.h"
#include "surveillance/rules/layering_rule.h"
#include "engine/cancel_on_disconnect.h"
#include "engine/gtd_expiry_checker.h"
#include "engine/startup.h"
#include "engine/snapshot_file.h"
#include "ops/metrics.h"
#include "config/json_config_loader.h"
#include "eod/eod_processor.h"
#include "eod/eod_file_writer.h"
#include "order_session_tracker.h"

#include <atomic>
#include <memory>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <filesystem>

// ─── Global shutdown flag ────────────────────────────────────────

static std::atomic<bool> g_shutdown{false};

static void signal_handler(int /*sig*/) {
    g_shutdown.store(true);
}

// ─── Config parsing ──────────────────────────────────────────────

using ConfigMap = std::unordered_map<std::string, std::string>;

static ConfigMap parse_config(const std::string& path) {
    ConfigMap cfg;
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "[FATAL] Cannot open config file: " << path << "\n";
        return cfg;
    }
    std::string line;
    while (std::getline(in, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto key = line.substr(0, eq);
        auto val = line.substr(eq + 1);
        // Trim whitespace
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!val.empty() && val.front() == ' ') val.erase(val.begin());
        cfg[key] = val;
    }
    return cfg;
}

static std::string cfg_get(const ConfigMap& cfg, const std::string& key, const std::string& def) {
    auto it = cfg.find(key);
    return (it != cfg.end()) ? it->second : def;
}

static uint16_t cfg_port(const ConfigMap& cfg, const std::string& key, uint16_t def) {
    auto it = cfg.find(key);
    if (it == cfg.end()) return def;
    return static_cast<uint16_t>(std::stoi(it->second));
}

static uint32_t cfg_uint(const ConfigMap& cfg, const std::string& key, uint32_t def) {
    auto it = cfg.find(key);
    if (it == cfg.end()) return def;
    return static_cast<uint32_t>(std::stoul(it->second));
}

// ─── Instrument loading (from JSON config) ──────────────────────

static bool load_instruments_from_config(
    const std::string& instruments_path,
    const std::string& sessions_path,
    refdata::RefDataService& ref_data,
    std::unordered_map<engine::PartitionId, std::unique_ptr<engine::Partition>>& partitions) {
    // Load instruments
    auto inst_result = config::JsonConfigLoader::load_instruments(instruments_path);
    if (!inst_result.success) {
        std::cerr << "[FATAL] Failed to load instruments: " << inst_result.error << "\n";
        return false;
    }

    for (const auto& entry : inst_result.instruments) {
        ref_data.add_instrument(entry.config);
        ref_data.register_symbol(entry.symbol, entry.instrument_id);
        ref_data.assign_to_partition(entry.instrument_id, entry.partition_id);

        // Store enriched instrument info for HTTP API
        refdata::RefDataService::InstrumentInfo info{};
        info.instrument_id = entry.instrument_id;
        info.symbol = entry.symbol;
        info.description = entry.description;
        info.currency = entry.currency;
        info.asset_class = entry.asset_class;
        info.config = entry.config;
        ref_data.set_instrument_info(entry.instrument_id, info);

        // Route instrument to the correct partition
        auto pit = partitions.find(entry.partition_id);
        if (pit == partitions.end()) {
            std::cerr << "[WARN] Instrument " << entry.instrument_id
                      << " references partition " << entry.partition_id
                      << " which does not exist — assigning to partition 1\n";
            pit = partitions.find(1);
            if (pit == partitions.end()) {
                std::cerr << "[FATAL] No partition 1 available for fallback\n";
                return false;
            }
        }
        pit->second->add_instrument(entry.config);
        std::cout << "[OK] Instrument " << entry.instrument_id
                  << " symbol='" << entry.symbol
                  << "' partition=" << entry.partition_id << "\n";
    }

    // Load sessions (informational for now — participant config logged)
    auto sess_result = config::JsonConfigLoader::load_sessions(sessions_path);
    if (!sess_result.success) {
        std::cerr << "[WARN] Failed to load sessions: " << sess_result.error << "\n";
        // Non-fatal: instruments are loaded, sessions are optional at this stage
    } else if (sess_result.session.has_value()) {
        const auto& session = *sess_result.session;
        std::cout << "[OK] Session profile: " << session.name
                  << " (" << session.timezone << ")\n";
        std::cout << "[OK] " << session.participants.size() << " participants configured\n";

        // Register participants in RefDataService for CompID → ParticipantId resolution
        for (const auto& pc : session.participants) {
            refdata::RefDataService::ParticipantInfo pinfo{};
            pinfo.id = pc.participant_id;
            pinfo.name = pc.name;
            pinfo.sender_comp_id = pc.sender_comp_id;
            ref_data.add_participant(pinfo);
            std::cout << "[OK] Participant " << pc.participant_id
                      << " comp_id='" << pc.sender_comp_id << "'\n";
        }
    }

    return true;
}

// ─── Market data partition listener ─────────────────────────────

class MdPartitionListener : public engine::PartitionListener {
public:
    MdPartitionListener(marketdata::MarketDataPublisher& pub, engine::Partition& partition,
                        marketdata::DepthPublisher& depth_pub, marketdata::StatisticsTracker& stats)
        : pub_(pub), partition_(partition), depth_pub_(depth_pub), stats_(stats) {}

    void set_eod_processor(eod::EodProcessor* proc) { eod_proc_ = proc; }
    void set_trade_event_bus(engine::TradeEventBus* bus) { trade_bus_ = bus; }
    void set_order_tracker(OrderSessionTracker* t) { order_tracker_ = t; }
    void set_trade_store(posttrade::TradeStore* store) { trade_store_ = store; }

    void on_result(engine::SequenceNum seq, const engine::PartitionMessage& msg,
                   const engine::MatchResult& result) override {
        // Helper: look up the passive participant_id via order tracker
        auto lookup_passive_pid = [this](const engine::MatchResult::Fill& f, engine::Side aggressor_side) -> engine::ParticipantId {
            engine::OrderId passive_oid = (aggressor_side == engine::Side::Buy)
                ? f.sell_order_id : f.buy_order_id;
            if (order_tracker_) {
                auto* info = order_tracker_->find(passive_oid);
                if (info) return info->participant_id;
            }
            return 0;
        };

        // Publish trades from fills — fills vector now populated by engine
        for (const auto& fill : result.fills) {
            pub_.on_trade(msg.instrument_id, fill.fill_price, fill.fill_qty,
                          fill.aggressor_side, fill.trade_id, msg.timestamp);

            // Track statistics (#99)
            stats_.on_trade(msg.instrument_id, fill.fill_price, fill.fill_qty);

            // Metrics (#104): count each engine match
            ops::Metrics::instance().increment("engine.matches.total");
            ops::Metrics::instance().increment("market_data.messages_published");

            // Feed trades to EOD processor
            if (eod_proc_) {
                engine::ParticipantId passive_pid = lookup_passive_pid(fill, fill.aggressor_side);

                eod::TradeRecord rec{};
                rec.trade_id = fill.trade_id;
                rec.instrument_id = msg.instrument_id;
                rec.buy_order_id = fill.buy_order_id;
                rec.sell_order_id = fill.sell_order_id;
                rec.buyer_id = (fill.aggressor_side == engine::Side::Buy) ? msg.participant_id : passive_pid;
                rec.seller_id = (fill.aggressor_side == engine::Side::Sell) ? msg.participant_id : passive_pid;
                rec.price = fill.fill_price;
                rec.quantity = fill.fill_qty;
                rec.timestamp = msg.timestamp;

                // Maker is the resting side, taker is the aggressor
                engine::ParticipantId maker_id = passive_pid;
                engine::ParticipantId taker_id = msg.participant_id;
                eod_proc_->record_trade(rec, maker_id, taker_id);
            }

            // Publish to trade event bus for external system integration
            if (trade_bus_) {
                engine::ParticipantId passive_pid_bus = lookup_passive_pid(fill, fill.aggressor_side);

                engine::TradeEvent te{};
                te.trade_id = fill.trade_id;
                te.instrument_id = msg.instrument_id;
                te.buy_order_id = fill.buy_order_id;
                te.sell_order_id = fill.sell_order_id;
                te.buyer_id = (fill.aggressor_side == engine::Side::Buy) ? msg.participant_id : passive_pid_bus;
                te.seller_id = (fill.aggressor_side == engine::Side::Sell) ? msg.participant_id : passive_pid_bus;
                te.price = fill.fill_price;
                te.quantity = fill.fill_qty;
                te.aggressor_side = fill.aggressor_side;
                te.timestamp = msg.timestamp;
                trade_bus_->publish(te);
            }
        }
        // Publish top-of-book change on any order activity
        if (result.action == engine::MatchResult::Action::Inserted ||
            result.action == engine::MatchResult::Action::Filled ||
            result.action == engine::MatchResult::Action::Cancelled) {
            // Get current book state for this instrument
            if (auto* eng = partition_.get_engine(msg.instrument_id)) {
                pub_.on_top_of_book_change(msg.instrument_id,
                    eng->book().best_bid(), eng->book().best_bid_qty(),
                    eng->book().best_ask(), eng->book().best_ask_qty(),
                    msg.timestamp);
                ops::Metrics::instance().increment("market_data.messages_published");

                // Publish depth snapshot after book change (#99)
                auto snap = depth_pub_.build_snapshot(eng->book());
                (void)snap;  // Snapshot delivered via sinks; depth_pub tracks sequence

                // Update resting order gauge (#104)
                ops::Metrics::instance().set_gauge("engine.orders.resting",
                    static_cast<int64_t>(eng->book().order_count()));
            }
        }

        // Record order event in trade store for order history queries
        if (trade_store_) {
            if (result.action == engine::MatchResult::Action::Inserted ||
                result.action == engine::MatchResult::Action::Filled ||
                result.action == engine::MatchResult::Action::Rejected ||
                result.action == engine::MatchResult::Action::Modified) {
                posttrade::OrderRecord orec{};
                switch (result.action) {
                    case engine::MatchResult::Action::Inserted: orec.event = posttrade::OrderRecord::Event::New; break;
                    case engine::MatchResult::Action::Filled:   orec.event = posttrade::OrderRecord::Event::Fill; break;
                    case engine::MatchResult::Action::Rejected:  orec.event = posttrade::OrderRecord::Event::Reject; break;
                    case engine::MatchResult::Action::Modified:  orec.event = posttrade::OrderRecord::Event::Modify; break;
                    default: break;
                }
                orec.order_id = result.assigned_order_id;
                orec.participant_id = msg.participant_id;
                orec.instrument_id = msg.instrument_id;
                orec.side = msg.side;
                orec.order_type = msg.order_type;
                orec.time_in_force = msg.time_in_force;
                orec.price = msg.price;
                orec.quantity = msg.quantity;
                orec.timestamp = msg.timestamp;
                trade_store_->record_order(orec);
            }
            // For cancels, record cancel event with proper participant info
            if (result.action == engine::MatchResult::Action::Cancelled) {
                for (auto cid : result.cancelled_ids) {
                    posttrade::OrderRecord orec{};
                    orec.event = posttrade::OrderRecord::Event::Cancel;
                    orec.order_id = cid;
                    orec.timestamp = msg.timestamp > 0 ? msg.timestamp : static_cast<engine::Timestamp>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count());
                    // Try order tracker first
                    if (order_tracker_) {
                        auto* info = order_tracker_->find(cid);
                        if (info) {
                            orec.participant_id = info->participant_id;
                            orec.instrument_id = info->instrument_id;
                            orec.side = info->side;
                        }
                    }
                    // Fallback: search trade_store for the original New event
                    if (orec.participant_id == 0 && trade_store_) {
                        for (engine::ParticipantId pid = 1; pid < 100; ++pid) {
                            auto orders = trade_store_->query_orders_by_participant(pid, 1000);
                            for (const auto& prev : orders) {
                                if (prev.order_id == cid && prev.event == posttrade::OrderRecord::Event::New) {
                                    orec.participant_id = prev.participant_id;
                                    orec.instrument_id = prev.instrument_id;
                                    orec.side = prev.side;
                                    orec.order_type = prev.order_type;
                                    orec.time_in_force = prev.time_in_force;
                                    break;
                                }
                            }
                            if (orec.participant_id != 0) break;
                        }
                    }
                    // Final fallback: use msg fields
                    if (orec.participant_id == 0) orec.participant_id = msg.participant_id;
                    if (orec.instrument_id == 0) orec.instrument_id = msg.instrument_id;
                    trade_store_->record_order(orec);
                }
            }
        }
    }

private:
    marketdata::MarketDataPublisher& pub_;
    engine::Partition& partition_;
    marketdata::DepthPublisher& depth_pub_;
    marketdata::StatisticsTracker& stats_;
    eod::EodProcessor* eod_proc_ = nullptr;
    engine::TradeEventBus* trade_bus_ = nullptr;
    OrderSessionTracker* order_tracker_ = nullptr;
    posttrade::TradeStore* trade_store_ = nullptr;
};

// ─── Main ────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // 1. Parse config path
    std::string config_path = "../deploy/config/platform.conf";
    if (argc > 1) {
        config_path = argv[1];
    }

    std::cout << "========================================\n";
    std::cout << "  Matching Engine Daemon — FR-1 Prototype\n";
    std::cout << "========================================\n";
    std::cout << "Config: " << config_path << "\n";

    // 2. Parse config
    auto cfg = parse_config(config_path);
    if (cfg.empty()) {
        std::cerr << "[FATAL] Empty or unreadable config. Exiting.\n";
        return 1;
    }

    const auto fix_port   = cfg_port(cfg, "fix.listen_port", 9876);
    const auto md_port    = cfg_port(cfg, "marketdata.listen_port", 9877);
    const auto ops_port   = cfg_port(cfg, "ops.listen_port", 8080);
    const auto pool_cap   = cfg_uint(cfg, "partition.pool_capacity", 100000);
    const auto journal_base = cfg_get(cfg, "journal.base_path", "./data/journal");
    const auto max_seg_str  = cfg_get(cfg, "journal.max_segment_bytes", "67108864");

    const auto partition_count = cfg_uint(cfg, "partition.count", 1);

    // 3. Create journal directories and partitions
    const auto journal_type = cfg_get(cfg, "journal.type", "segmented");

    // Storage for per-partition journals (kept alive for daemon lifetime)
    std::vector<std::unique_ptr<engine::SegmentedJournal>> seg_journals;
    std::vector<std::unique_ptr<engine::DirectIOJournal>> dio_journals;

    // Cancel-on-disconnect handler
    engine::CancelOnDisconnect cod_handler;

    // Order-to-session tracker for passive fill notifications
    OrderSessionTracker order_tracker;

    // Partition map: partition_id → Partition
    std::unordered_map<engine::PartitionId, std::unique_ptr<engine::Partition>> partitions;

    for (uint32_t i = 1; i <= partition_count; ++i) {
        auto pid = static_cast<engine::PartitionId>(i);
        auto part_journal_path = journal_base + "/partition_" + std::to_string(i);
        std::filesystem::create_directories(part_journal_path);

        engine::IJournal* journal_ptr = nullptr;

        if (journal_type == "direct_io") {
            engine::DirectIOJournal::Config dcfg;
            dcfg.base_path = part_journal_path;
            dcfg.max_segment_bytes = std::stoull(max_seg_str);
            dcfg.alignment = cfg_uint(cfg, "journal.alignment", 4096);

            auto dio = std::make_unique<engine::DirectIOJournal>(dcfg);
            if (!dio->open()) {
                std::cerr << "[FATAL] Failed to open direct_io journal at: " << part_journal_path << "\n";
                return 1;
            }
            journal_ptr = dio.get();
            dio_journals.push_back(std::move(dio));
            std::cout << "[OK] DirectIO journal for partition " << pid << " at " << part_journal_path << "\n";
        } else {
            engine::SegmentedJournal::Config jcfg;
            jcfg.base_path = part_journal_path;
            jcfg.max_segment_bytes = std::stoull(max_seg_str);
            jcfg.flush_mode = engine::FileJournal::FlushMode::SYNC;

            auto seg = std::make_unique<engine::SegmentedJournal>(jcfg);
            if (!seg->open()) {
                std::cerr << "[FATAL] Failed to open journal at: " << part_journal_path << "\n";
                return 1;
            }
            journal_ptr = seg.get();
            seg_journals.push_back(std::move(seg));
            std::cout << "[OK] Segmented journal for partition " << pid << " at " << part_journal_path << "\n";
        }

        partitions[pid] = std::make_unique<engine::Partition>(pid, pool_cap, journal_ptr);
    }

    std::cout << "[OK] " << partition_count << " partition(s) created\n";

    // --- RefData ---
    refdata::RefDataService ref_data;
    const auto config_dir = std::filesystem::path(config_path).parent_path().string();
    const auto instruments_path = cfg_get(cfg, "instruments.config_path",
                                           config_dir + "/instruments.json");
    const auto sessions_path = cfg_get(cfg, "sessions.config_path",
                                        config_dir + "/sessions.json");

    if (!load_instruments_from_config(instruments_path, sessions_path, ref_data, partitions)) {
        return 1;
    }
    std::cout << "[OK] " << ref_data.instrument_count() << " instruments loaded\n";
    ops::Metrics::instance().set_gauge("partition.instrument_count",
                                        static_cast<int64_t>(ref_data.instrument_count()));

    // --- Trade Store (created early for snapshot restore) ---
    posttrade::TradeStore trade_store;

    // --- Warm Restart: load snapshot file to rebuild order book state ---
    // Snapshot-based recovery is immune to struct layout changes (unlike journal replay).
    // The snapshot file stores each order field individually in a versioned binary format.
    const auto snapshot_dir = cfg_get(cfg, "snapshot.dir", "./pilot-data/snapshots");
    const auto snapshot_path = snapshot_dir + "/latest.snap";
    std::filesystem::create_directories(snapshot_dir);

    std::cout << "[OK] Attempting snapshot load for warm restart...\n";
    std::cout.flush();
    {
        auto snap = engine::SnapshotFileReader::load(snapshot_path);
        if (snap) {
            uint64_t total_restored = 0;
            for (const auto& pd : snap->partitions) {
                auto pit = partitions.find(pd.partition_id);
                if (pit == partitions.end()) {
                    std::cout << "[WARN] Snapshot references partition " << pd.partition_id
                              << " which does not exist — skipping\n";
                    continue;
                }
                auto& part = *pit->second;
                for (const auto& od : pd.orders) {
                    auto* eng = part.get_engine_mut(od.instrument_id);
                    if (!eng) continue;

                    engine::Order* order = part.pool().allocate();
                    if (!order) {
                        std::cerr << "[WARN] Pool exhausted during snapshot restore\n";
                        break;
                    }
                    order->clear();
                    order->instrument_id    = od.instrument_id;
                    order->order_id         = od.order_id;
                    order->client_order_id  = od.client_order_id;
                    order->participant_id   = od.participant_id;
                    order->account_id       = od.account_id;
                    order->session_id       = od.session_id;
                    order->side             = od.side;
                    order->order_type       = od.order_type;
                    order->time_in_force    = od.time_in_force;
                    order->price            = od.price;
                    order->original_qty     = od.original_qty;
                    order->remaining_qty    = od.remaining_qty;
                    order->display_qty      = od.display_qty;
                    order->cumulative_filled = od.cumulative_filled;
                    order->stop_price       = od.stop_price;
                    order->trail_offset     = od.trail_offset;
                    order->peg_type         = od.peg_type;
                    order->peg_offset       = od.peg_offset;
                    order->peg_limit        = od.peg_limit;
                    order->budget           = od.budget;
                    order->budget_spent     = od.budget_spent;
                    order->stp_group_id     = od.stp_group_id;
                    order->flags            = od.flags;
                    order->entry_sequence   = od.entry_sequence;
                    order->entry_timestamp  = od.entry_timestamp;
                    order->expiry_time      = od.expiry_time;

                    eng->book().insert_without_matching(order);
                    ++total_restored;

                    // Record in TradeStore so "Recent Orders" shows restored orders
                    posttrade::OrderRecord orec{};
                    orec.event = posttrade::OrderRecord::Event::New;
                    orec.order_id = order->order_id;
                    orec.participant_id = order->participant_id;
                    orec.instrument_id = order->instrument_id;
                    orec.side = order->side;
                    orec.order_type = order->order_type;
                    orec.time_in_force = order->time_in_force;
                    orec.price = order->price;
                    orec.quantity = order->original_qty;
                    orec.timestamp = order->entry_timestamp;
                    trade_store.record_order(orec);
                }
                // Restore partition sequence counter
                part.set_sequence(pd.sequence);
            }
            std::cout << "[OK] Snapshot loaded: " << total_restored << " orders restored\n";
        } else {
            std::cout << "[OK] No snapshot found, clean start\n";
        }
        std::cout.flush();
    }

    // --- Risk ---
    risk::BasicRiskService risk_svc;

    // --- Market Data ---
    marketdata::MarketDataPublisher md_pub;
    marketdata::MdTcpServer md_server(md_port);
    md_pub.add_sink(&md_server);

    // --- Multicast UDP publisher (#99) ---
    const auto md_multicast_group = cfg_get(cfg, "marketdata.multicast_group", "239.1.1.1");
    const auto md_multicast_port  = cfg_port(cfg, "marketdata.multicast_port", 9878);
    marketdata::MdUdpPublisher::Config udp_config;
    udp_config.multicast_group = md_multicast_group;
    udp_config.port = md_multicast_port;
    marketdata::MdUdpPublisher udp_publisher(udp_config);
    md_pub.add_sink(&udp_publisher);
    std::cout << "[OK] Multicast MD publisher: " << md_multicast_group
              << ":" << md_multicast_port << "\n";

    // --- Depth publisher and statistics tracker (#99) ---
    marketdata::DepthPublisher depth_publisher;
    marketdata::StatisticsTracker stats_tracker;

    // Wire snapshot provider: new MD subscribers receive current top-of-book state
    md_server.set_snapshot_provider([&partitions]() -> std::string {
        std::ostringstream oss;
        oss << "{\"type\":\"snapshot\",\"instruments\":[";
        bool first = true;
        for (const auto& [pid, part] : partitions) {
            for (const auto& [id, eng] : part->engines()) {
                if (!first) oss << ",";
                first = false;
                oss << "{\"id\":" << id
                    << ",\"bid\":" << eng.book().best_bid()
                    << ",\"bid_qty\":" << eng.book().best_bid_qty()
                    << ",\"ask\":" << eng.book().best_ask()
                    << ",\"ask_qty\":" << eng.book().best_ask_qty()
                    << "}";
            }
        }
        oss << "]}";
        return oss.str();
    });

    // Create a market data listener per partition
    std::vector<std::unique_ptr<MdPartitionListener>> md_listeners;
    for (auto& [pid, part] : partitions) {
        auto listener = std::make_unique<MdPartitionListener>(md_pub, *part, depth_publisher, stats_tracker);
        part->set_listener(listener.get());
        md_listeners.push_back(std::move(listener));
    }

    // Wire order tracker to listeners for passive participant lookup
    for (auto& listener : md_listeners) {
        listener->set_order_tracker(&order_tracker);
    }

    // --- EOD ---
    eod::EodProcessor eod_proc;
    const auto eod_output_dir = cfg_get(cfg, "eod.output_dir", "./data/eod");
    for (auto& listener : md_listeners) {
        listener->set_eod_processor(&eod_proc);
    }
    std::cout << "[OK] EOD processor wired (output=" << eod_output_dir << ")\n";

    // --- Trade Event Bus (integration with external systems) ---
    engine::TradeEventBus trade_event_bus;
    for (auto& listener : md_listeners) {
        listener->set_trade_event_bus(&trade_event_bus);
    }
    std::cout << "[OK] Trade event bus wired\n";

    // --- Trade Store (wiring — already created above for snapshot restore) ---
    for (auto& listener : md_listeners) {
        listener->set_trade_store(&trade_store);
    }

    // Subscribe trade event bus → update positions + trade store on every fill
    trade_event_bus.subscribe("position_tracker", [&risk_svc](const engine::TradeEvent& evt) {
        // Update buyer position (buy = +qty)
        risk_svc.position_tracker().on_fill(evt.buyer_id, evt.instrument_id, engine::Side::Buy, evt.quantity);
        // Update seller position (sell = -qty)
        risk_svc.position_tracker().on_fill(evt.seller_id, evt.instrument_id, engine::Side::Sell, evt.quantity);
    });
    trade_event_bus.subscribe("trade_store", [&trade_store](const engine::TradeEvent& evt) {
        posttrade::TradeRecord rec;
        rec.trade_id = evt.trade_id;
        rec.buy_order_id = evt.buy_order_id;
        rec.sell_order_id = evt.sell_order_id;
        rec.buyer_id = evt.buyer_id;
        rec.seller_id = evt.seller_id;
        rec.instrument_id = evt.instrument_id;
        rec.price = evt.price;
        rec.quantity = evt.quantity;
        rec.aggressor_side = evt.aggressor_side;
        rec.timestamp = evt.timestamp;
        trade_store.record_trade(rec);
    });
    std::cout << "[OK] Trade store + position tracking wired to event bus\n";

    // --- GTD Expiry Checker ---
    // System clock for GTD order expiry
    struct SystemClock : engine::ITimeSource {
        [[nodiscard]] engine::Timestamp now() const noexcept override {
            return static_cast<engine::Timestamp>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
        }
    } system_clock;

    engine::GtdExpiryChecker gtd_checker(system_clock);
    for (auto& [pid, part] : partitions) {
        gtd_checker.register_partition(part.get());
    }

    // Timer thread: check GTD expiry every second
    std::atomic<bool> gtd_running{true};
    std::thread gtd_thread([&gtd_checker, &gtd_running]() {
        while (gtd_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            uint32_t cancelled = gtd_checker.tick();
            if (cancelled > 0) {
                std::cout << "[GTD] Expired and cancelled " << cancelled << " orders\n";
            }
        }
    });
    std::cout << "[OK] GTD expiry checker running (1s interval)\n";

    // --- Periodic snapshot save thread (every 60 seconds) ---
    std::atomic<bool> snapshot_running{true};
    std::thread snapshot_thread([&partitions, &snapshot_path, &snapshot_running]() {
        while (snapshot_running) {
            std::this_thread::sleep_for(std::chrono::seconds(60));
            if (!snapshot_running) break;
            if (engine::SnapshotFileWriter::save(snapshot_path, partitions)) {
                std::cout << "[SNAPSHOT] Periodic snapshot saved\n";
            } else {
                std::cerr << "[WARN] Periodic snapshot save failed\n";
            }
        }
    });
    std::cout << "[OK] Snapshot persistence running (60s interval, path=" << snapshot_path << ")\n";

    // --- Surveillance Engine ---
    surveillance::SurveillanceEngine surveillance_engine;
    surveillance_engine.add_rule(std::make_unique<surveillance::WashTradeRule>());
    surveillance_engine.add_rule(std::make_unique<surveillance::LayeringRule>());
    trade_event_bus.subscribe("surveillance", [&surveillance_engine](const engine::TradeEvent& evt) {
        surveillance_engine.on_trade(evt.buyer_id, evt.seller_id, evt.instrument_id,
                                     evt.price, evt.quantity, evt.timestamp);
    });
    std::cout << "[OK] Surveillance engine wired (" << surveillance_engine.rule_count() << " rules)\n";

    // --- Trade Audit Trail ---
    const auto audit_trail_path = cfg_get(cfg, "log.dir", "./pilot-data/logs") + "/trade_audit.dat";
    ops::TradeAuditTrail trade_audit_trail(audit_trail_path);
    trade_event_bus.subscribe("trade_audit", [&trade_audit_trail](const engine::TradeEvent& evt) {
        trade_audit_trail.on_fill(evt.buyer_id, evt.seller_id, evt.instrument_id,
                                  evt.buy_order_id, evt.sell_order_id,
                                  evt.trade_id, evt.price, evt.quantity, evt.timestamp);
    });
    std::cout << "[OK] Trade audit trail: " << audit_trail_path << "\n";

    // --- Audit Log ---
    const auto audit_path = cfg_get(cfg, "log.dir", "./pilot-data/logs") + "/audit.jsonl";
    ops::AuditLog audit_log(audit_path);
    std::cout << "[OK] Audit log: " << audit_path << "\n";

    // --- Ops ---
    ops::OpsController ops_ctrl;
    for (auto& [pid, part] : partitions) {
        ops_ctrl.register_partition(part.get());
    }
    ops_ctrl.set_eod_processor(&eod_proc, eod_output_dir);
    ops_ctrl.set_audit_log(&audit_log);
    ops_ctrl.set_trade_store(&trade_store);
    ops_ctrl.set_surveillance_engine(&surveillance_engine);
    ops_ctrl.set_trade_audit_trail(&trade_audit_trail);

    refdata::RefDataHttpHandler refdata_http(ref_data);
    refdata_http.set_partitions(&partitions);
    ops::HttpServer http_server(ops_port);

    // CORS origin: restrict to specific origin if configured
    const auto cors_origin = cfg_get(cfg, "ops.cors_origin", "*");
    http_server.set_cors_origin(cors_origin);
    if (cors_origin != "*") {
        std::cout << "[OK] CORS restricted to: " << cors_origin << "\n";
    }

    http_server.wire(ops_ctrl, refdata_http);

    // --- Auth Service (portal login) ---
    ops::AuthService auth_svc;

    // JWT signing key: prefer environment variable, fall back to config
    const auto signing_key_env = std::getenv("JWT_SIGNING_KEY");
    if (signing_key_env) {
        auth_svc.set_signing_key(signing_key_env);
        std::cout << "[OK] JWT signing key loaded from JWT_SIGNING_KEY environment variable\n";
    }

    // API key: prefer environment variable, fall back to config file
    const auto api_key_env = std::getenv("OPS_API_KEY");
    const auto api_key = api_key_env ? std::string(api_key_env) : cfg_get(cfg, "ops.api_key", "");
    if (api_key_env) {
        std::cout << "[OK] API key loaded from OPS_API_KEY environment variable\n";
    } else if (!api_key.empty()) {
        std::cout << "[OK] API key loaded from config file\n";
    }

    if (!api_key.empty()) {
        ops::AuthMiddleware auth;
        auth.add_api_key(api_key);
        auth.set_open_paths({
            "/api/v1/health",
            "/api/v1/instruments",
            "/api/v1/metrics",
            "/api/v1/metrics/json",
            "/api/v1/auth/login",
            "/api/v1/auth/me",
            "/api/v1/fix/sessions",
            "/api/v1/eod/status",
            "/api/v1/kill-switch",
            "/api/v1/partitions",
            "/api/v1/audit",
            "/api/v1/audit/trades",
            "/api/v1/surveillance/alerts",
            "/api/v1/settlement/status",
        });
        auth.set_token_validator([&auth_svc](const std::string& token) {
            return auth_svc.validate(token).has_value();
        });
        http_server.set_auth(auth);
        std::cout << "[OK] Ops auth enabled (API key + JWT)\n";
    } else {
        std::cout << "[WARN] Ops auth DISABLED (no ops.api_key in config)\n";
    }

    // --- Auth Service users ---
    const auto portal_user = cfg_get(cfg, "portal.username", "admin");
    const auto portal_pass = cfg_get(cfg, "portal.password", api_key);  // Default to API key
    auth_svc.add_user(portal_user, portal_pass, "admin");
    auth_svc.add_user("operator", api_key, "operator");
    auth_svc.add_user("viewer", "viewer", "viewer");
    ops_ctrl.set_auth_service(&auth_svc);
    ops_ctrl.set_risk_service(&risk_svc);
    std::cout << "[OK] Portal auth service configured (users: " << portal_user << ", operator, viewer)\n";

    // --- FIX Gateway ---
    gateway::RefDataGatewayBridge bridge(ref_data);
    gateway::RealFixAdapter fix_adapter = bridge.create_adapter();

    gateway::FixTcpAcceptor fix_acceptor(fix_port);

    // --- FIX Message Store (persistent or in-memory) ---
    const auto msg_store_dir = cfg_get(cfg, "fix.message_store_dir", "");
    std::unique_ptr<gateway::FileMessageStore> file_msg_store;
    std::unique_ptr<gateway::FixMessageStore>  mem_msg_store;
    if (!msg_store_dir.empty()) {
        file_msg_store = std::make_unique<gateway::FileMessageStore>(msg_store_dir);
        uint32_t loaded = file_msg_store->load();
        fix_acceptor.set_message_store(&file_msg_store->inner());
        std::cout << "[OK] FIX message store: persistent (dir=" << msg_store_dir
                  << ", loaded=" << loaded << " messages)\n";
    } else {
        mem_msg_store = std::make_unique<gateway::FixMessageStore>();
        fix_acceptor.set_message_store(mem_msg_store.get());
        std::cout << "[OK] FIX message store: in-memory\n";
    }

    // Wire FIX session query for portal
    ops_ctrl.set_session_query([&fix_acceptor]() -> std::string {
        return fix_acceptor.sessions_json();
    });

    // --- TLS on FIX ---
    const auto tls_enabled = cfg_get(cfg, "tls.enabled", "false");
    if (tls_enabled == "true") {
        transport::TlsConfig tls_cfg;
        tls_cfg.cert_file = cfg_get(cfg, "tls.cert_file", "");
        tls_cfg.key_file = cfg_get(cfg, "tls.key_file", "");
        tls_cfg.ca_file = cfg_get(cfg, "tls.ca_file", "");
        tls_cfg.require_client_cert = (cfg_get(cfg, "tls.require_client_cert", "false") == "true");
        if (tls_cfg.cert_file.empty() || tls_cfg.key_file.empty()) {
            std::cerr << "[FATAL] TLS enabled but cert_file or key_file not set\n";
            return 1;
        }
        fix_acceptor.set_tls_config(tls_cfg);
        std::cout << "[OK] TLS enabled on FIX port (cert=" << tls_cfg.cert_file << ")\n";
    }

    // Wire FIX callbacks: route application messages to adapter → partition
    gateway::FixTcpCallbacks fix_callbacks;
    fix_callbacks.on_message = [&](engine::SessionId session_id, const std::string& raw_fix) {
        ops::Metrics::instance().increment("fix.messages_received");
        auto fields = gateway::FixParser::parse_fields(raw_fix);
        auto msg_type = gateway::FixParser::get_msg_type(fields);
        if (!msg_type) return;

        if (*msg_type == "D") {
            auto cmd = fix_adapter.parse_new_order(raw_fix);
            if (!cmd) {
                auto sym_it = fields.find(55);
                std::string sym_val = (sym_it != fields.end()) ? sym_it->second : "(missing)";
                std::cout << "[FIX] event=parse_fail session=" << session_id
                          << " msg_type=D reason=missing_required_fields symbol=" << sym_val
                          << "\n";
                auto reject = fix_adapter.build_reject(0, engine::RejectCode::UnknownInstrument);
                fix_acceptor.send(session_id, reject);
                ops::Metrics::instance().increment("fix.orders_rejected");
                return;
            }

            // Build partition message
            engine::PartitionMessage pm{};
            pm.type = engine::PartitionMessage::Type::NewOrder;
            pm.timestamp = 0;  // Would use real clock in production
            pm.client_order_id = cmd->client_order_id;
            pm.instrument_id = cmd->instrument_id;
            pm.side = cmd->side;
            pm.order_type = cmd->order_type;
            pm.time_in_force = cmd->time_in_force;
            pm.price = cmd->price;
            pm.quantity = cmd->quantity;
            pm.participant_id = cmd->participant_id;
            pm.account_id = cmd->account_id;
            pm.session_id = session_id;
            pm.stp_group_id = cmd->stp_group_id;
            pm.flags = cmd->flags;
            pm.expiry_time = 0;

            // Route to correct partition based on instrument's partition assignment
            auto target_pid = ref_data.get_partition(cmd->instrument_id);
            if (target_pid == 0) target_pid = 1;  // Fallback
            auto pit = partitions.find(target_pid);
            if (pit == partitions.end()) {
                auto reject = fix_adapter.build_reject(cmd->client_order_id, engine::RejectCode::UnknownInstrument);
                fix_acceptor.send(session_id, reject);
                ops::Metrics::instance().increment("fix.orders_rejected");
                return;
            }
            auto result = pit->second->process(pm);
            ops::Metrics::instance().increment("journal.entries_written");

            // Send ack or reject back via FIX
            if (result.action == engine::MatchResult::Action::Rejected) {
                ops::Metrics::instance().increment("fix.orders_rejected");
                std::cout << "[FIX] event=order_rejected session=" << session_id
                          << " clord=" << cmd->client_order_id
                          << " inst=" << cmd->instrument_id
                          << " reason=" << static_cast<int>(result.reject_code)
                          << "\n";
                auto reject = fix_adapter.build_reject(cmd->client_order_id, result.reject_code);
                fix_acceptor.send(session_id, reject);
            } else {
                ops::Metrics::instance().increment("fix.orders_accepted");

                // Track this order for passive fill routing
                if (result.action == engine::MatchResult::Action::Inserted ||
                    result.action == engine::MatchResult::Action::Filled) {
                    OrderSessionInfo info;
                    info.session_id = session_id;
                    info.participant_id = cmd->participant_id;
                    info.side = cmd->side;
                    info.instrument_id = cmd->instrument_id;
                    info.original_qty = cmd->quantity;
                    info.raw_client_order_id = cmd->raw_client_order_id;
                    order_tracker.track(result.assigned_order_id, info);
                }

                if (result.action == engine::MatchResult::Action::Filled) {
                    // Send fill responses — fills vector now populated by engine
                    engine::Quantity cum_qty = 0;
                    for (const auto& fill : result.fills) {
                        cum_qty += fill.fill_qty;
                        std::cout << "[FIX] event=trade session=" << session_id
                                  << " clord=" << cmd->client_order_id
                                  << " inst=" << cmd->instrument_id
                                  << " side=" << static_cast<int>(cmd->side)
                                  << " price=" << fill.fill_price
                                  << " qty=" << fill.fill_qty
                                  << " action=" << static_cast<int>(result.action)
                                  << " fills=" << result.fills.size()
                                  << "\n";
                        auto fill_msg = fix_adapter.build_fill(
                            result.assigned_order_id, fill.fill_price, fill.fill_qty,
                            cum_qty, cmd->quantity - cum_qty, cmd->side);
                        fix_acceptor.send(session_id, fill_msg);

                        // ── Passive fill notification ──────────────────
                        engine::OrderId passive_order_id =
                            (fill.aggressor_side == engine::Side::Buy)
                                ? fill.sell_order_id : fill.buy_order_id;

                        OrderSessionInfo passive_info;
                        if (order_tracker.snapshot_and_fill(passive_order_id,
                                fill.fill_qty, passive_info)) {
                            engine::Quantity passive_leaves =
                                passive_info.original_qty - passive_info.cumulative_filled;
                            auto passive_fill = fix_adapter.build_fill(
                                passive_order_id, fill.fill_price, fill.fill_qty,
                                passive_info.cumulative_filled, passive_leaves,
                                passive_info.side);
                            fix_acceptor.send(passive_info.session_id, passive_fill);

                            std::cout << "[FIX] event=passive_fill session="
                                      << passive_info.session_id
                                      << " order_id=" << passive_order_id
                                      << " price=" << fill.fill_price
                                      << " qty=" << fill.fill_qty
                                      << "\n";

                            if (passive_leaves == 0) {
                                order_tracker.untrack(passive_order_id);
                            }
                        }
                    }

                    // Handle STP cancels — notify passive participants
                    for (auto cancelled_id : result.cancelled_ids) {
                        OrderSessionInfo cancelled_info;
                        if (order_tracker.snapshot_and_fill(cancelled_id, 0, cancelled_info)) {
                            if (cancelled_info.session_id != session_id) {
                                auto cancel_msg = fix_adapter.build_cancel_ack(cancelled_id);
                                fix_acceptor.send(cancelled_info.session_id, cancel_msg);
                                std::cout << "[FIX] event=stp_cancel_notify session="
                                          << cancelled_info.session_id
                                          << " order_id=" << cancelled_id << "\n";
                            }
                        }
                        order_tracker.untrack(cancelled_id);
                    }

                    // If aggressor fully filled, untrack it too
                    if (cum_qty >= cmd->quantity) {
                        order_tracker.untrack(result.assigned_order_id);
                    }
                } else {
                    std::cout << "[FIX] event=order_accepted session=" << session_id
                              << " clord=" << cmd->client_order_id
                              << " inst=" << cmd->instrument_id
                              << " side=" << static_cast<int>(cmd->side)
                              << " price=" << cmd->price
                              << " qty=" << cmd->quantity
                              << " action=" << static_cast<int>(result.action)
                              << " fills=" << result.fills.size()
                              << "\n";
                    // Order resting — send new ack
                    auto ack = fix_adapter.build_new_ack(*cmd, result.assigned_order_id);
                    fix_acceptor.send(session_id, ack);
                }
            }
        } else if (*msg_type == "F") {
            auto cmd = fix_adapter.parse_cancel(raw_fix);
            if (!cmd) {
                std::cout << "[FIX] event=parse_fail session=" << session_id
                          << " msg_type=F reason=missing_required_fields"
                          << "\n";
                auto reject = fix_adapter.build_reject(0, engine::RejectCode::None);
                fix_acceptor.send(session_id, reject);
                return;
            }

            engine::PartitionMessage pm{};
            pm.type = engine::PartitionMessage::Type::Cancel;
            pm.timestamp = 0;
            pm.client_order_id = cmd->client_order_id;
            pm.instrument_id = cmd->instrument_id;
            pm.orig_order_id = cmd->orig_order_id;
            pm.session_id = session_id;

            // Route cancel to correct partition (extract from order ID)
            auto cancel_part_id = static_cast<engine::PartitionId>(cmd->orig_order_id >> 48);
            if (cancel_part_id == 0) cancel_part_id = 1;
            auto cpit = partitions.find(cancel_part_id);
            if (cpit == partitions.end()) {
                auto reject = fix_adapter.build_reject(cmd->client_order_id, engine::RejectCode::UnknownInstrument);
                fix_acceptor.send(session_id, reject);
                return;
            }
            auto result = cpit->second->process(pm);
            ops::Metrics::instance().increment("journal.entries_written");

            if (result.action == engine::MatchResult::Action::Cancelled) {
                ops::Metrics::instance().increment("fix.cancels_processed");
                std::cout << "[FIX] event=cancel_ok session=" << session_id
                          << " orig_order_id=" << cmd->orig_order_id
                          << "\n";
                auto ack = fix_adapter.build_cancel_ack(cmd->orig_order_id);
                fix_acceptor.send(session_id, ack);
                order_tracker.untrack(cmd->orig_order_id);
            } else {
                std::cout << "[FIX] event=cancel_fail session=" << session_id
                          << " orig_order_id=" << cmd->orig_order_id
                          << " reason=" << static_cast<int>(result.reject_code)
                          << "\n";
                auto reject = fix_adapter.build_reject(cmd->client_order_id, result.reject_code);
                fix_acceptor.send(session_id, reject);
            }

        } else if (*msg_type == "G") {
            // OrderCancelReplaceRequest
            auto cmd = fix_adapter.parse_modify(raw_fix);
            if (!cmd) {
                auto reject = fix_adapter.build_reject(0, engine::RejectCode::None);
                fix_acceptor.send(session_id, reject);
                return;
            }

            engine::PartitionMessage pm{};
            pm.type = engine::PartitionMessage::Type::Modify;
            pm.timestamp = 0;
            pm.client_order_id = cmd->client_order_id;
            pm.instrument_id = cmd->instrument_id;
            pm.orig_order_id = cmd->orig_order_id;
            pm.price = cmd->new_price;
            pm.quantity = cmd->new_quantity;
            pm.session_id = session_id;

            auto mod_part_id = static_cast<engine::PartitionId>(cmd->orig_order_id >> 48);
            if (mod_part_id == 0) mod_part_id = 1;
            auto mpit = partitions.find(mod_part_id);
            if (mpit == partitions.end()) {
                auto reject = fix_adapter.build_reject(cmd->client_order_id, engine::RejectCode::UnknownInstrument);
                fix_acceptor.send(session_id, reject);
                return;
            }
            auto result = mpit->second->process(pm);
            ops::Metrics::instance().increment("journal.entries_written");

            if (result.action == engine::MatchResult::Action::Rejected) {
                auto reject = fix_adapter.build_reject(cmd->client_order_id, result.reject_code);
                fix_acceptor.send(session_id, reject);
            } else {
                auto ack = fix_adapter.build_new_ack(
                    gateway::NewOrderCommand{cmd->client_order_id, {}, cmd->instrument_id,
                        engine::Side::Buy, engine::OrderType::Limit, engine::TimeInForce::Day,
                        cmd->new_price, cmd->new_quantity, 0, 0, session_id, 0, 0},
                    result.assigned_order_id);
                fix_acceptor.send(session_id, ack);
            }

        } else if (*msg_type == "H") {
            // OrderStatusRequest
            auto cmd = fix_adapter.parse_order_status_request(raw_fix);
            if (!cmd) {
                auto reject = fix_adapter.build_business_reject("H", "Invalid order status request");
                fix_acceptor.send(session_id, reject);
                return;
            }

            // Search partitions for the order
            bool found = false;
            for (const auto& [pid, part] : partitions) {
                for (const auto& [inst_id, eng] : part->engines()) {
                    // Try to find order by ID in the book
                    auto* order = eng.book().find_order(cmd->order_id);
                    if (order) {
                        auto resp = fix_adapter.build_order_status_response(
                            cmd->order_id, order->side, order->price,
                            order->remaining_qty, 0, "0");  // 0=New (resting)
                        fix_acceptor.send(session_id, resp);
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
            if (!found) {
                auto resp = fix_adapter.build_order_status_response(
                    cmd->order_id, engine::Side::Buy, 0, 0, 0, "C");  // C=Expired/Unknown
                fix_acceptor.send(session_id, resp);
            }

        } else if (*msg_type == "q") {
            // OrderMassCancelRequest
            auto cmd = fix_adapter.parse_mass_cancel_request(raw_fix);
            if (!cmd) {
                auto reject = fix_adapter.build_business_reject("q", "Invalid mass cancel request");
                fix_acceptor.send(session_id, reject);
                return;
            }

            uint32_t count = 0;
            // Collect order IDs first, then cancel (avoids iterating while modifying)
            for (auto& [pid, part] : partitions) {
                std::vector<std::pair<engine::InstrumentId, std::vector<engine::OrderId>>> to_cancel;
                for (const auto& [inst_id, eng] : part->engines()) {
                    if (cmd->scope == gateway::MassCancelCommand::Scope::ByInstrument &&
                        inst_id != cmd->instrument_id) {
                        continue;
                    }
                    auto orders = eng.book().all_order_ids();
                    if (!orders.empty()) {
                        to_cancel.emplace_back(inst_id, std::move(orders));
                    }
                }
                for (const auto& [inst_id, orders] : to_cancel) {
                    for (auto oid : orders) {
                        engine::PartitionMessage cancel_pm{};
                        cancel_pm.type = engine::PartitionMessage::Type::Cancel;
                        cancel_pm.orig_order_id = oid;
                        cancel_pm.instrument_id = inst_id;
                        cancel_pm.session_id = session_id;
                        auto result = part->process(cancel_pm);
                        if (result.action == engine::MatchResult::Action::Cancelled) {
                            ++count;
                            order_tracker.untrack(oid);
                        }
                    }
                }
            }

            auto report = fix_adapter.build_mass_cancel_report(cmd->client_order_id, count);
            fix_acceptor.send(session_id, report);
            std::cout << "[FIX] event=mass_cancel session=" << session_id
                      << " cancelled=" << count << "\n";

        } else if (*msg_type == "V") {
            // MarketDataRequest — build and send snapshot
            auto symbol_it = fields.find(55);
            if (symbol_it == fields.end()) {
                auto reject = fix_adapter.build_business_reject("V", "Missing Symbol");
                fix_acceptor.send(session_id, reject);
                return;
            }
            auto inst_id = ref_data.resolve_symbol(symbol_it->second);
            if (inst_id == 0) {
                auto reject = fix_adapter.build_business_reject("V", "Unknown instrument");
                fix_acceptor.send(session_id, reject);
                return;
            }

            // Find the engine for this instrument
            std::vector<std::pair<engine::Price, engine::Quantity>> bids, asks;
            auto target_pid = ref_data.get_partition(inst_id);
            if (target_pid == 0) target_pid = 1;
            auto pit = partitions.find(target_pid);
            if (pit != partitions.end()) {
                auto* eng = pit->second->get_engine(inst_id);
                if (eng) {
                    bids = eng->book().bid_levels();
                    asks = eng->book().ask_levels();
                }
            }

            auto snapshot = fix_adapter.build_market_data_snapshot(inst_id, bids, asks);
            fix_acceptor.send(session_id, snapshot);

        } else {
            // Unknown message type — send BusinessMessageReject
            auto reject = fix_adapter.build_business_reject(*msg_type, "Unsupported message type");
            fix_acceptor.send(session_id, reject);
        }
    };

    // Track active FIX session count for gauge (#104)
    std::atomic<int64_t> active_fix_sessions{0};

    fix_callbacks.on_logon = [&](engine::SessionId sid, engine::ParticipantId pid) {
        ops::Metrics::instance().increment("fix.sessions_logon");
        auto count = active_fix_sessions.fetch_add(1) + 1;
        ops::Metrics::instance().set_gauge("fix.active_sessions", count);
        fix_adapter.on_session_logon(sid, pid);
        std::cout << "[FIX] event=logon session=" << sid
                  << " participant=" << pid << "\n";
    };

    fix_callbacks.on_logout = [&](engine::SessionId sid) {
        auto count = active_fix_sessions.fetch_sub(1) - 1;
        ops::Metrics::instance().set_gauge("fix.active_sessions", count);
        fix_adapter.on_session_logout(sid);
        std::cout << "[FIX] event=logout session=" << sid << "\n";
    };

    fix_callbacks.on_disconnect = [&](engine::SessionId sid) {
        ops::Metrics::instance().increment("fix.sessions_disconnect");
        fix_adapter.on_session_disconnect(sid);

        // Cancel-on-disconnect: cancel all resting orders for this session
        auto cod_cancels = cod_handler.on_disconnect(sid);
        for (engine::OrderId oid : cod_cancels) {
            // Route cancel through the partition that owns this order
            auto target_pid = static_cast<engine::PartitionId>(oid >> 48);
            auto pit = partitions.find(target_pid);
            if (pit != partitions.end()) {
                engine::PartitionMessage cancel_msg{};
                cancel_msg.type = engine::PartitionMessage::Type::Cancel;
                cancel_msg.orig_order_id = oid;
                cancel_msg.instrument_id = 0;  // Partition will look up by order ID
                pit->second->process(cancel_msg);
            }
            order_tracker.untrack(oid);
        }
        if (!cod_cancels.empty()) {
            std::cout << "[CoD] cancelled " << cod_cancels.size()
                      << " orders for session=" << sid << "\n";
        }

        std::cout << "[FIX] event=disconnect session=" << sid << "\n";
    };

    fix_acceptor.set_callbacks(fix_callbacks);

    // 5. Install signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 6. Start multicast UDP publisher
    if (!udp_publisher.start()) {
        std::cerr << "[WARN] Multicast UDP publisher failed to start — continuing without multicast\n";
    }

    // 7. Start services in background threads
    std::thread md_thread([&]() {
        if (!md_server.start()) {
            std::cerr << "[ERROR] Market data server failed to start on port " << md_port << "\n";
        }
    });

    std::thread http_thread([&]() {
        if (!http_server.start()) {
            std::cerr << "[ERROR] HTTP server failed to start on port " << ops_port << "\n";
        }
    });

    std::thread fix_thread([&]() {
        if (!fix_acceptor.start()) {
            std::cerr << "[ERROR] FIX acceptor failed to start on port " << fix_port << "\n";
        }
    });

    // 7. Print startup banner
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "  Matching Engine is RUNNING\n";
    std::cout << "========================================\n";
    std::cout << "  FIX port:         " << fix_port << "\n";
    std::cout << "  Market data port: " << md_port << "\n";
    std::cout << "  Multicast MD:     " << md_multicast_group << ":" << md_multicast_port << "\n";
    std::cout << "  Ops/HTTP port:    " << ops_port << "\n";
    std::cout << "  Partitions:       " << partition_count << "\n";
    std::cout << "  Instruments:      " << ref_data.instrument_count() << "\n";
    std::cout << "  Journal:          " << journal_base << "\n";
    std::cout << "========================================\n";
    std::cout << "Press Ctrl+C to shutdown.\n\n";

    // 8. Wait for shutdown signal — also check for automatic EOD trigger
    bool eod_triggered_today = false;
    while (!g_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Automatic EOD: if all instruments are Closed and EOD hasn't triggered yet
        if (!eod_triggered_today) {
            bool all_closed = true;
            uint32_t inst_count = 0;
            for (const auto& [pid, part] : partitions) {
                for (const auto& [id, eng] : part->engines()) {
                    ++inst_count;
                    if (eng.state() != engine::InstrumentState::Closed) {
                        all_closed = false;
                        break;
                    }
                }
                if (!all_closed) break;
            }
            if (inst_count > 0 && all_closed) {
                std::cout << "[EOD] Automatic trigger — all instruments closed\n";
                ops_ctrl.handle({"POST", "/api/v1/eod/trigger", "", {}, {}});
                eod_triggered_today = true;
            }
        }
    }

    std::cout << "\n[SHUTDOWN] Signal received. Shutting down...\n";

    // 9. Graceful shutdown in reverse order
    fix_acceptor.stop();
    std::cout << "[SHUTDOWN] FIX acceptor stopped\n";

    http_server.stop();
    std::cout << "[SHUTDOWN] HTTP server stopped\n";

    udp_publisher.stop();
    std::cout << "[SHUTDOWN] Multicast UDP publisher stopped\n";

    md_server.stop();
    std::cout << "[SHUTDOWN] Market data server stopped\n";

    // Save final snapshot before closing journals
    if (engine::SnapshotFileWriter::save(snapshot_path, partitions)) {
        std::cout << "[SHUTDOWN] Final snapshot saved\n";
    } else {
        std::cerr << "[SHUTDOWN] WARNING: Final snapshot save failed!\n";
    }

    for (auto& j : seg_journals) { j->flush(); j->close(); }
    for (auto& j : dio_journals) { j->flush(); j->close(); }
    std::cout << "[SHUTDOWN] Journals flushed and closed\n";

    // Join background threads
    gtd_running = false;
    if (gtd_thread.joinable()) gtd_thread.join();
    snapshot_running = false;
    if (snapshot_thread.joinable()) snapshot_thread.join();
    if (fix_thread.joinable()) fix_thread.join();
    if (http_thread.joinable()) http_thread.join();
    if (md_thread.joinable()) md_thread.join();

    std::cout << "[SHUTDOWN] Complete. Goodbye.\n";
    return 0;
}
