/// Matching Engine Daemon — Milestone 12 standalone service entry point.
///
/// Wires together the full runtime stack:
///   RefData → Risk → Journal → Partition → MatchingEngine
///   FIX TCP Acceptor → RealFixAdapter → Partition
///   Partition → MarketDataPublisher → MdTcpServer
///   OpsController + RefDataHttpHandler → HttpServer
///
/// Usage: matching-engine-daemon [config-path]
///   Default config: ../deploy/config/platform.conf

#include "engine/partition.h"
#include "engine/matching.h"
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
#include "ops/ops_controller.h"
#include "ops/http/http_server.h"
#include "ops/auth_middleware.h"
#include "ops/auth_service.h"
#include "ops/audit_log.h"
#include "ops/metrics.h"
#include "config/json_config_loader.h"
#include "eod/eod_processor.h"
#include "eod/eod_file_writer.h"

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

static bool load_instruments_from_config(const std::string& instruments_path,
                                          const std::string& sessions_path,
                                          refdata::RefDataService& ref_data,
                                          engine::Partition& partition) {
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
        partition.add_instrument(entry.config);
        std::cout << "[OK] Instrument " << entry.instrument_id
                  << " symbol='" << entry.symbol << "'\n";
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
    }

    return true;
}

// ─── Market data partition listener ─────────────────────────────

class MdPartitionListener : public engine::PartitionListener {
public:
    MdPartitionListener(marketdata::MarketDataPublisher& pub, engine::Partition& partition)
        : pub_(pub), partition_(partition) {}

    void set_eod_processor(eod::EodProcessor* proc) { eod_proc_ = proc; }

    void on_result(engine::SequenceNum seq, const engine::PartitionMessage& msg,
                   const engine::MatchResult& result) override {
        // Publish trades from fills — fills vector now populated by engine
        for (const auto& fill : result.fills) {
            pub_.on_trade(msg.instrument_id, fill.fill_price, fill.fill_qty,
                          fill.aggressor_side, fill.trade_id, msg.timestamp);

            // Feed trades to EOD processor
            if (eod_proc_) {
                eod::TradeRecord rec{};
                rec.trade_id = fill.trade_id;
                rec.instrument_id = msg.instrument_id;
                rec.buy_order_id = fill.buy_order_id;
                rec.sell_order_id = fill.sell_order_id;
                rec.buyer_id = (fill.aggressor_side == engine::Side::Buy) ? msg.participant_id : 0;
                rec.seller_id = (fill.aggressor_side == engine::Side::Sell) ? msg.participant_id : 0;
                rec.price = fill.fill_price;
                rec.quantity = fill.fill_qty;
                rec.timestamp = msg.timestamp;

                // Maker is the resting side, taker is the aggressor
                engine::ParticipantId maker_id = 0;
                engine::ParticipantId taker_id = msg.participant_id;
                eod_proc_->record_trade(rec, maker_id, taker_id);
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
            }
        }
    }

private:
    marketdata::MarketDataPublisher& pub_;
    engine::Partition& partition_;
    eod::EodProcessor* eod_proc_ = nullptr;
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
    const auto part_id    = static_cast<engine::PartitionId>(cfg_uint(cfg, "partition.id", 1));
    const auto journal_base = cfg_get(cfg, "journal.base_path", "./data/journal");
    const auto max_seg_str  = cfg_get(cfg, "journal.max_segment_bytes", "67108864");

    // 3. Create journal directory
    std::filesystem::create_directories(journal_base);

    // 4. Wire up components
    // --- Journal (type selectable via config: "segmented" or "direct_io") ---
    const auto journal_type = cfg_get(cfg, "journal.type", "segmented");

    std::unique_ptr<engine::SegmentedJournal> seg_journal;
    std::unique_ptr<engine::DirectIOJournal> dio_journal;
    engine::IJournal* journal_ptr = nullptr;

    if (journal_type == "direct_io") {
        engine::DirectIOJournal::Config dcfg;
        dcfg.base_path = journal_base;
        dcfg.max_segment_bytes = std::stoull(max_seg_str);
        dcfg.alignment = cfg_uint(cfg, "journal.alignment", 4096);

        dio_journal = std::make_unique<engine::DirectIOJournal>(dcfg);
        if (!dio_journal->open()) {
            std::cerr << "[FATAL] Failed to open direct_io journal at: " << journal_base << "\n";
            return 1;
        }
        journal_ptr = dio_journal.get();
        std::cout << "[OK] DirectIO journal opened at " << journal_base << "\n";
    } else {
        engine::SegmentedJournal::Config jcfg;
        jcfg.base_path = journal_base;
        jcfg.max_segment_bytes = std::stoull(max_seg_str);
        jcfg.flush_mode = engine::FileJournal::FlushMode::SYNC;

        seg_journal = std::make_unique<engine::SegmentedJournal>(jcfg);
        if (!seg_journal->open()) {
            std::cerr << "[FATAL] Failed to open journal at: " << journal_base << "\n";
            return 1;
        }
        journal_ptr = seg_journal.get();
        std::cout << "[OK] Segmented journal opened at " << journal_base << "\n";
    }

    // --- Partition ---
    engine::Partition partition(part_id, pool_cap, journal_ptr);

    // --- RefData ---
    refdata::RefDataService ref_data;
    const auto config_dir = std::filesystem::path(config_path).parent_path().string();
    const auto instruments_path = cfg_get(cfg, "instruments.config_path",
                                           config_dir + "/instruments.json");
    const auto sessions_path = cfg_get(cfg, "sessions.config_path",
                                        config_dir + "/sessions.json");

    if (!load_instruments_from_config(instruments_path, sessions_path, ref_data, partition)) {
        return 1;
    }
    std::cout << "[OK] " << ref_data.instrument_count() << " instruments loaded\n";
    ops::Metrics::instance().set_gauge("partition.instrument_count",
                                        static_cast<int64_t>(ref_data.instrument_count()));

    // --- Risk ---
    risk::BasicRiskService risk_svc;

    // --- Market Data ---
    marketdata::MarketDataPublisher md_pub;
    marketdata::MdTcpServer md_server(md_port);
    md_pub.add_sink(&md_server);

    MdPartitionListener md_listener(md_pub, partition);
    partition.set_listener(&md_listener);

    // --- EOD ---
    eod::EodProcessor eod_proc;
    const auto eod_output_dir = cfg_get(cfg, "eod.output_dir", "./data/eod");
    md_listener.set_eod_processor(&eod_proc);
    std::cout << "[OK] EOD processor wired (output=" << eod_output_dir << ")\n";

    // --- Audit Log ---
    const auto audit_path = cfg_get(cfg, "log.dir", "./pilot-data/logs") + "/audit.jsonl";
    ops::AuditLog audit_log(audit_path);
    std::cout << "[OK] Audit log: " << audit_path << "\n";

    // --- Ops ---
    ops::OpsController ops_ctrl;
    ops_ctrl.register_partition(&partition);
    ops_ctrl.set_eod_processor(&eod_proc, eod_output_dir);
    ops_ctrl.set_audit_log(&audit_log);

    refdata::RefDataHttpHandler refdata_http(ref_data);
    ops::HttpServer http_server(ops_port);
    http_server.wire(ops_ctrl, refdata_http);

    // --- Auth Service (portal login) ---
    ops::AuthService auth_svc;
    const auto api_key = cfg_get(cfg, "ops.api_key", "");
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

            auto result = partition.process(pm);

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

            auto result = partition.process(pm);

            if (result.action == engine::MatchResult::Action::Cancelled) {
                ops::Metrics::instance().increment("fix.cancels_processed");
                std::cout << "[FIX] event=cancel_ok session=" << session_id
                          << " orig_order_id=" << cmd->orig_order_id
                          << "\n";
                auto ack = fix_adapter.build_cancel_ack(cmd->orig_order_id);
                fix_acceptor.send(session_id, ack);
            } else {
                std::cout << "[FIX] event=cancel_fail session=" << session_id
                          << " orig_order_id=" << cmd->orig_order_id
                          << " reason=" << static_cast<int>(result.reject_code)
                          << "\n";
                auto reject = fix_adapter.build_reject(cmd->client_order_id, result.reject_code);
                fix_acceptor.send(session_id, reject);
            }
        }
    };

    fix_callbacks.on_logon = [&](engine::SessionId sid, engine::ParticipantId pid) {
        ops::Metrics::instance().increment("fix.sessions_logon");
        fix_adapter.on_session_logon(sid, pid);
        std::cout << "[FIX] event=logon session=" << sid
                  << " participant=" << pid << "\n";
    };

    fix_callbacks.on_logout = [&](engine::SessionId sid) {
        fix_adapter.on_session_logout(sid);
        std::cout << "[FIX] event=logout session=" << sid << "\n";
    };

    fix_callbacks.on_disconnect = [&](engine::SessionId sid) {
        ops::Metrics::instance().increment("fix.sessions_disconnect");
        fix_adapter.on_session_disconnect(sid);
        std::cout << "[FIX] event=disconnect session=" << sid << "\n";
    };

    fix_acceptor.set_callbacks(fix_callbacks);

    // 5. Install signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 6. Start services in background threads
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
    std::cout << "  Ops/HTTP port:    " << ops_port << "\n";
    std::cout << "  Partition ID:     " << part_id << "\n";
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
            for (const auto& [id, eng] : partition.engines()) {
                ++inst_count;
                if (eng.state() != engine::InstrumentState::Closed) {
                    all_closed = false;
                    break;
                }
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

    md_server.stop();
    std::cout << "[SHUTDOWN] Market data server stopped\n";

    journal_ptr->flush();
    if (seg_journal) seg_journal->close();
    if (dio_journal) dio_journal->close();
    std::cout << "[SHUTDOWN] Journal flushed and closed\n";

    // Join background threads
    if (fix_thread.joinable()) fix_thread.join();
    if (http_thread.joinable()) http_thread.join();
    if (md_thread.joinable()) md_thread.join();

    std::cout << "[SHUTDOWN] Complete. Goodbye.\n";
    return 0;
}
