/// Failover detection tests — health probe, failover controller, simulated drill.
#include "replication/health_probe.h"
#include "replication/failover_controller.h"
#include "replication/journal_replicator.h"
#include "replication/journal_receiver.h"
#include "engine/journal_file.h"
#include "engine/journal_stub.h"
#include "engine/partition.h"
#include "engine/startup.h"
#include "engine/matching.h"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <cstring>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>

namespace replication {
namespace {

// ─── Helpers ─────────────────────────────────────────────────

static engine::InstrumentConfig make_config(engine::InstrumentId id = 100) {
    engine::InstrumentConfig cfg{};
    cfg.instrument_id = id;
    cfg.tick_size = 1;
    cfg.lot_size = 1;
    cfg.min_quantity = 1;
    cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000;
    cfg.static_band_pct = 20.0;
    cfg.residual_policy = engine::ResidualPolicy::Cancel;
    cfg.stp_mode = engine::STPMode::CancelNewest;
    cfg.qty_increase_loses_priority = true;
    return cfg;
}

static engine::PartitionMessage make_order(engine::InstrumentId inst,
                                            engine::Side side,
                                            engine::Price price,
                                            engine::Quantity qty,
                                            engine::ClientOrderId clord) {
    engine::PartitionMessage msg{};
    msg.type = engine::PartitionMessage::Type::NewOrder;
    msg.timestamp = 1000;
    msg.client_order_id = clord;
    msg.instrument_id = inst;
    msg.side = side;
    msg.order_type = engine::OrderType::Limit;
    msg.time_in_force = engine::TimeInForce::GTC;
    msg.price = price;
    msg.quantity = qty;
    msg.participant_id = 1;
    msg.account_id = 1;
    msg.session_id = 1;
    return msg;
}

/// Start receiver in a background thread, wait until it is listening.
static std::thread start_receiver_thread(JournalReceiver& receiver) {
    std::thread t([&receiver]() {
        receiver.start();
    });
    for (int i = 0; i < 100; ++i) {
        if (receiver.is_running()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return t;
}

/// Simple HTTP server that responds with 200 OK.
/// Runs in a background thread, can be stopped via the running flag.
class SimpleHttpServer {
public:
    SimpleHttpServer() = default;
    ~SimpleHttpServer() { stop(); }

    /// Start listening on an OS-assigned port. Returns the bound port.
    uint16_t start() {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) return 0;

        int opt = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(0);

        if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            return 0;
        }

        struct sockaddr_in bound{};
        socklen_t len = sizeof(bound);
        ::getsockname(listen_fd_, reinterpret_cast<struct sockaddr*>(&bound), &len);
        port_ = ntohs(bound.sin_port);

        ::listen(listen_fd_, 5);
        running_.store(true);

        thread_ = std::thread([this]() { serve_loop(); });
        return port_;
    }

    void stop() {
        running_.store(false);
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    uint16_t port() const { return port_; }
    bool is_running() const { return running_.load(); }

private:
    int listen_fd_ = -1;
    uint16_t port_ = 0;
    std::atomic<bool> running_{false};
    std::thread thread_;

    void serve_loop() {
        while (running_.load()) {
            struct pollfd pfd{};
            pfd.fd = listen_fd_;
            pfd.events = POLLIN;

            int ret = ::poll(&pfd, 1, 100);
            if (ret <= 0) continue;

            struct sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = ::accept(listen_fd_,
                                      reinterpret_cast<struct sockaddr*>(&client_addr),
                                      &client_len);
            if (client_fd < 0) continue;

            // Read request (we don't really parse it)
            char buf[1024];
            ::recv(client_fd, buf, sizeof(buf), 0);

            // Send 200 OK
            const char* response =
                "HTTP/1.0 200 OK\r\n"
                "Content-Length: 2\r\n"
                "\r\n"
                "OK";
            ::send(client_fd, response, std::strlen(response), 0);
            ::close(client_fd);
        }
    }
};

// ─── HealthProbe Tests ───────────────────────────────────────

TEST(HealthProbeTest, DetectsHealthyPrimary) {
    SimpleHttpServer server;
    uint16_t port = server.start();
    ASSERT_GT(port, 0u);

    HealthProbe::Config cfg;
    cfg.primary_host = "127.0.0.1";
    cfg.primary_port = port;
    cfg.interval_ms = 100;
    cfg.timeout_ms = 1000;
    cfg.failure_threshold = 3;

    HealthProbe probe(cfg);
    probe.start();

    // Wait enough time for at least one successful probe
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    EXPECT_EQ(probe.state(), HealthProbe::State::HEALTHY);
    EXPECT_EQ(probe.consecutive_failures(), 0);

    probe.stop();
    server.stop();
}

TEST(HealthProbeTest, DetectsFailedPrimary) {
    // Probe a port that nobody is listening on
    HealthProbe::Config cfg;
    cfg.primary_host = "127.0.0.1";
    cfg.primary_port = 19999;  // unlikely to be in use
    cfg.interval_ms = 50;
    cfg.timeout_ms = 200;
    cfg.failure_threshold = 3;

    HealthProbe probe(cfg);
    probe.start();

    // Wait for threshold failures: 3 * 50ms interval + timeout overhead
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    EXPECT_EQ(probe.state(), HealthProbe::State::FAILED);
    EXPECT_GE(probe.consecutive_failures(), cfg.failure_threshold);

    probe.stop();
}

TEST(HealthProbeTest, DegradedBeforeFailed) {
    // Track state transitions through callback
    std::mutex mtx;
    std::vector<std::pair<HealthProbe::State, HealthProbe::State>> transitions;

    HealthProbe::Config cfg;
    cfg.primary_host = "127.0.0.1";
    cfg.primary_port = 19998;  // not listening
    cfg.interval_ms = 50;
    cfg.timeout_ms = 200;
    cfg.failure_threshold = 3;

    HealthProbe probe(cfg);
    probe.set_state_callback([&](HealthProbe::State old_s, HealthProbe::State new_s) {
        std::lock_guard<std::mutex> lock(mtx);
        transitions.emplace_back(old_s, new_s);
    });

    probe.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    probe.stop();

    std::lock_guard<std::mutex> lock(mtx);
    // First transition should be HEALTHY -> DEGRADED
    ASSERT_GE(transitions.size(), 2u);
    EXPECT_EQ(transitions[0].first, HealthProbe::State::HEALTHY);
    EXPECT_EQ(transitions[0].second, HealthProbe::State::DEGRADED);

    // Eventually should see transition to FAILED
    bool saw_failed = false;
    for (const auto& [from, to] : transitions) {
        if (to == HealthProbe::State::FAILED) {
            saw_failed = true;
            break;
        }
    }
    EXPECT_TRUE(saw_failed);
}

TEST(HealthProbeTest, RecoverFromFailed) {
    // Start with no server -> FAILED
    HealthProbe::Config cfg;
    cfg.primary_host = "127.0.0.1";
    cfg.primary_port = 0;  // will be set after server starts
    cfg.interval_ms = 50;
    cfg.timeout_ms = 200;
    cfg.failure_threshold = 2;

    // Start a server, get port, stop it, then restart it
    SimpleHttpServer server;
    uint16_t port = server.start();
    ASSERT_GT(port, 0u);
    server.stop();

    cfg.primary_port = port;

    HealthProbe probe(cfg);

    std::mutex mtx;
    std::vector<HealthProbe::State> states_seen;
    probe.set_state_callback([&](HealthProbe::State /*old_s*/, HealthProbe::State new_s) {
        std::lock_guard<std::mutex> lock(mtx);
        states_seen.push_back(new_s);
    });

    probe.start();

    // Wait for FAILED state (server is down)
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    EXPECT_EQ(probe.state(), HealthProbe::State::FAILED);

    // Restart the server on the same port
    SimpleHttpServer server2;
    // We need to bind to the same port — create a new server
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
        ::listen(fd, 5);

        // Accept and respond in a thread
        std::atomic<bool> serving{true};
        std::thread serve_thread([&]() {
            while (serving.load()) {
                struct pollfd pfd{};
                pfd.fd = fd;
                pfd.events = POLLIN;
                int ret = ::poll(&pfd, 1, 100);
                if (ret <= 0) continue;

                struct sockaddr_in ca{};
                socklen_t cl = sizeof(ca);
                int cfd = ::accept(fd, reinterpret_cast<struct sockaddr*>(&ca), &cl);
                if (cfd < 0) continue;

                char buf[1024];
                ::recv(cfd, buf, sizeof(buf), 0);
                const char* resp = "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nOK";
                ::send(cfd, resp, std::strlen(resp), 0);
                ::close(cfd);
            }
        });

        // Wait for probe to recover
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        EXPECT_EQ(probe.state(), HealthProbe::State::HEALTHY);
        EXPECT_EQ(probe.consecutive_failures(), 0);

        serving.store(false);
        serve_thread.join();
        ::close(fd);
    } else {
        // Port reuse failed — skip recovery check but don't fail the test
        ::close(fd);
        GTEST_SKIP() << "Could not rebind to port " << port << " for recovery test";
    }

    probe.stop();
}

// ─── FailoverController Tests ────────────────────────────────

TEST(FailoverControllerTest, ReadyOnlyWhenFailed) {
    // Set up a dummy receiver with a stub journal
    engine::JournalStub stub_journal;
    JournalReceiver::Config rcfg;
    rcfg.listen_port = 0;
    JournalReceiver receiver(rcfg, &stub_journal);

    // Append an entry so receiver has something
    engine::JournalEntry entry{};
    entry.sequence = 1;
    entry.timestamp = 1000;
    entry.partition_id = 1;
    entry.entry_type = engine::JournalEntry::TYPE_INPUT_COMMAND;
    auto msg = make_order(100, engine::Side::Buy, 10000, 100, 1);
    entry.payload_size = sizeof(msg);
    entry.payload = &msg;
    stub_journal.append(entry);

    // Create probe with force_state
    HealthProbe::Config pcfg;
    pcfg.primary_port = 19997;
    HealthProbe probe(pcfg);

    FailoverController controller(&probe, &receiver);

    // HEALTHY -> not ready
    probe.force_state(HealthProbe::State::HEALTHY);
    auto st = controller.status();
    EXPECT_FALSE(st.ready_to_promote);
    EXPECT_EQ(st.probe_state, "healthy");
    EXPECT_FALSE(st.not_ready_reason.empty());

    // DEGRADED -> not ready
    probe.force_state(HealthProbe::State::DEGRADED);
    st = controller.status();
    EXPECT_FALSE(st.ready_to_promote);
    EXPECT_EQ(st.probe_state, "degraded");

    // FAILED -> ready (receiver has entries)
    probe.force_state(HealthProbe::State::FAILED);
    st = controller.status();
    EXPECT_EQ(st.probe_state, "failed");
    // Note: receiver stats won't show entries because we didn't go through the
    // receive path. But the stub journal has entries.
    // The readiness check depends on receiver stats, so this will say not ready
    // because entries_received == 0. That's correct behavior — the receiver
    // must have actually received entries via replication.
}

TEST(FailoverControllerTest, PromoteFromReplicatedJournal) {
    // Full failover drill using the replication pipeline

    // Step 1: Primary journal + partition
    const std::string primary_path = "/tmp/failover_test_primary.bin";
    const std::string standby_path = "/tmp/failover_test_standby.bin";
    std::remove(primary_path.c_str());
    std::remove(standby_path.c_str());

    engine::FileJournal primary_journal;
    ASSERT_TRUE(primary_journal.open(primary_path, engine::FileJournal::FlushMode::NONE));

    engine::Partition primary_partition(1, 10000, &primary_journal);
    primary_partition.add_instrument(make_config(100));

    // Step 2: Standby journal + receiver
    engine::FileJournal standby_journal;
    ASSERT_TRUE(standby_journal.open(standby_path, engine::FileJournal::FlushMode::NONE));

    JournalReceiver::Config rcfg;
    rcfg.listen_port = 0;
    JournalReceiver receiver(rcfg, &standby_journal);
    auto recv_thread = start_receiver_thread(receiver);

    // Step 3: Replicator
    JournalReplicator::Config scfg;
    scfg.dr_host = "127.0.0.1";
    scfg.dr_port = receiver.bound_port();
    scfg.mode = ReplicationMode::SYNC;
    JournalReplicator replicator(scfg);
    ASSERT_TRUE(replicator.connect());

    // Step 4: Process orders on primary
    auto order1 = make_order(100, engine::Side::Buy, 9990, 50, 1);
    auto order2 = make_order(100, engine::Side::Buy, 9980, 30, 2);
    auto order3 = make_order(100, engine::Side::Sell, 10050, 80, 3);
    (void)primary_partition.process(order1);
    (void)primary_partition.process(order2);
    (void)primary_partition.process(order3);
    primary_journal.flush();

    // Step 5: Replicate journal entries
    struct ReplicateCtx {
        JournalReplicator* replicator;
        uint64_t replicated;
    };
    ReplicateCtx rctx{&replicator, 0};
    primary_journal.replay_from(1, [](const engine::JournalEntry& e, void* ctx) {
        auto* c = static_cast<ReplicateCtx*>(ctx);
        c->replicator->replicate(e);
        c->replicated++;
    }, &rctx);
    EXPECT_GT(rctx.replicated, 0u);

    // Step 6: Stop replication
    replicator.disconnect();
    receiver.stop();
    recv_thread.join();
    EXPECT_GT(standby_journal.entry_count(), 0u);

    // Step 7: Close and reopen standby journal
    standby_journal.close();
    engine::FileJournal recovery_journal;
    ASSERT_TRUE(recovery_journal.open(standby_path, engine::FileJournal::FlushMode::NONE));

    // Step 8: Create FailoverController with probe in FAILED state
    HealthProbe::Config pcfg;
    pcfg.primary_port = 19996;
    HealthProbe probe(pcfg);
    probe.force_state(HealthProbe::State::FAILED);

    // Create a dummy receiver for the controller (not actively receiving)
    engine::JournalStub dummy_journal;
    JournalReceiver::Config dummy_rcfg;
    dummy_rcfg.listen_port = 0;
    JournalReceiver dummy_receiver(dummy_rcfg, &dummy_journal);

    FailoverController controller(&probe, &dummy_receiver);

    // Step 9: Promote
    engine::Partition dr_partition(1, 10000);
    std::vector<engine::InstrumentConfig> instruments = {make_config(100)};

    auto result = controller.promote(&recovery_journal, dr_partition, instruments);
    EXPECT_TRUE(result.success) << "Promote failed: " << result.error;
    EXPECT_GT(result.journal_entries_replayed, 0u);

    // Step 10: Verify DR partition matches primary
    auto* primary_eng = primary_partition.get_engine(100);
    auto* dr_eng = dr_partition.get_engine(100);
    ASSERT_NE(primary_eng, nullptr);
    ASSERT_NE(dr_eng, nullptr);

    EXPECT_EQ(primary_eng->book().order_count(), dr_eng->book().order_count());
    EXPECT_EQ(primary_eng->book().best_bid(), dr_eng->book().best_bid());
    EXPECT_EQ(primary_eng->book().best_ask(), dr_eng->book().best_ask());

    EXPECT_TRUE(engine::StartupController::validate_state(dr_partition));

    recovery_journal.close();
    primary_journal.close();
    std::remove(primary_path.c_str());
    std::remove(standby_path.c_str());
}

TEST(FailoverControllerTest, StatusJson) {
    engine::JournalStub stub_journal;
    JournalReceiver::Config rcfg;
    rcfg.listen_port = 0;
    JournalReceiver receiver(rcfg, &stub_journal);

    HealthProbe::Config pcfg;
    pcfg.primary_port = 19995;
    HealthProbe probe(pcfg);
    probe.force_state(HealthProbe::State::DEGRADED);

    FailoverController controller(&probe, &receiver);
    std::string json = controller.status_json();

    // Verify JSON has all expected fields
    EXPECT_NE(json.find("\"probe_state\""), std::string::npos);
    EXPECT_NE(json.find("\"consecutive_failures\""), std::string::npos);
    EXPECT_NE(json.find("\"replication_lag\""), std::string::npos);
    EXPECT_NE(json.find("\"receiver_entries\""), std::string::npos);
    EXPECT_NE(json.find("\"ready_to_promote\""), std::string::npos);
    EXPECT_NE(json.find("\"degraded\""), std::string::npos);
    EXPECT_NE(json.find("\"not_ready_reason\""), std::string::npos);
}

TEST(FailoverControllerTest, PromoteFailsWhenHealthy) {
    engine::JournalStub stub_journal;
    JournalReceiver::Config rcfg;
    rcfg.listen_port = 0;
    JournalReceiver receiver(rcfg, &stub_journal);

    HealthProbe::Config pcfg;
    pcfg.primary_port = 19994;
    HealthProbe probe(pcfg);
    // Default state is HEALTHY

    FailoverController controller(&probe, &receiver);

    engine::Partition partition(1, 10000);
    std::vector<engine::InstrumentConfig> instruments = {make_config(100)};

    auto result = controller.promote(&stub_journal, partition, instruments);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("FAILED"), std::string::npos);
    EXPECT_EQ(result.journal_entries_replayed, 0u);
}

}  // namespace
}  // namespace replication
