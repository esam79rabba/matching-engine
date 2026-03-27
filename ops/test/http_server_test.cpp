/// HTTP server tests — unit tests for request parsing and response building.
/// No actual TCP connections — tests the handler routing logic.
#include "ops/http/http_server.h"
#include "ops/ops_controller.h"
#include "refdata/ref_data_http.h"
#include "refdata/ref_data_service.h"
#include "engine/journal_stub.h"
#include <gtest/gtest.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace ops {
namespace {

class HttpServerTest : public ::testing::Test {
protected:
    engine::JournalStub journal{1000};
    engine::Partition partition{1, 1000, &journal};
    OpsController ops_ctrl;
    refdata::RefDataService ref_svc;
    refdata::RefDataHttpHandler ref_handler{ref_svc};

    void SetUp() override {
        engine::InstrumentConfig cfg{};
        cfg.instrument_id = 100; cfg.tick_size = 1; cfg.lot_size = 1;
        cfg.min_quantity = 1; cfg.max_quantity = 1000000;
        cfg.static_ref_price = 50000; cfg.static_band_pct = 20.0;
        cfg.residual_policy = engine::ResidualPolicy::Cancel;
        cfg.stp_mode = engine::STPMode::CancelNewest;
        cfg.qty_increase_loses_priority = true;

        partition.add_instrument(cfg);
        ref_svc.add_instrument(cfg);
        ref_svc.register_symbol("BTC-USD", 100);
        ops_ctrl.register_partition(&partition);
    }
};

TEST_F(HttpServerTest, WiredHandlerRoutesHealth) {
    HttpServer server(0);  // Port 0 = don't actually bind
    server.wire(ops_ctrl, ref_handler);

    // Simulate handler call directly (no TCP)
    // The wire() method creates a lambda we can test
    // For this, we call the handler_ directly through the server's public interface
    // Since handler_ is private, we test through the ops/refdata handlers directly
    auto ops_resp = ops_ctrl.handle({"GET", "/api/v1/health", "", {}, {}});
    EXPECT_EQ(ops_resp.status, 200);
    EXPECT_NE(ops_resp.body.find("\"status\":\"ok\""), std::string::npos);
}

TEST_F(HttpServerTest, WiredHandlerRoutesRefData) {
    auto resp = ref_handler.handle({"GET", "/api/v1/instruments/100", ""});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"instrument_id\":100"), std::string::npos);
}

TEST_F(HttpServerTest, WiredHandlerRoutesSymbolLookup) {
    auto resp = ref_handler.handle({"GET", "/api/v1/symbols/BTC-USD", ""});
    EXPECT_EQ(resp.status, 200);
    EXPECT_NE(resp.body.find("\"instrument_id\":100"), std::string::npos);
}

TEST_F(HttpServerTest, UnknownRouteReturns404) {
    auto resp = ref_handler.handle({"GET", "/api/v1/nonexistent", ""});
    EXPECT_EQ(resp.status, 404);
}

TEST_F(HttpServerTest, HaltResumeThroughOps) {
    auto halt = ops_ctrl.handle({"POST", "/api/v1/instruments/100/halt", "", {}, {}});
    EXPECT_EQ(halt.status, 200);
    EXPECT_NE(halt.body.find("\"state\":\"halted\""), std::string::npos);

    auto resume = ops_ctrl.handle({"POST", "/api/v1/instruments/100/resume", "", {}, {}});
    EXPECT_EQ(resume.status, 200);
    EXPECT_NE(resume.body.find("\"state\":\"continuous\""), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
// Tier 2.2 — Multi-threaded HTTP server
// ═══════════════════════════════════════════════════════════════

TEST_F(HttpServerTest, ConcurrentRequests) {
    // Send 5 HTTP requests in parallel threads, verify all get responses.
    // This exercises the multi-threaded accept loop.

    HttpServer server(0);  // Let OS pick a free port
    server.wire(ops_ctrl, ref_handler);

    // We need to bind to get the actual port. Start in a thread.
    // Use a known port range for testing.
    uint16_t test_port = 18900 + (::getpid() % 100);
    HttpServer real_server(test_port);
    real_server.wire(ops_ctrl, ref_handler);

    std::thread server_thread([&real_server]() {
        real_server.start();
    });

    // Give server time to start listening
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Send 5 concurrent HTTP requests
    constexpr int NUM_REQUESTS = 5;
    std::vector<std::thread> client_threads;
    std::atomic<int> success_count{0};
    std::atomic<int> response_count{0};

    for (int i = 0; i < NUM_REQUESTS; ++i) {
        client_threads.emplace_back([&]() {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) return;

            struct sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(test_port);

            if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
                ::close(sock);
                return;
            }

            std::string request = "GET /api/v1/health HTTP/1.1\r\nHost: localhost\r\n\r\n";
            ::send(sock, request.c_str(), request.size(), 0);

            char buf[4096];
            // Set recv timeout
            struct timeval tv{2, 0};
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
            ::close(sock);

            if (n > 0) {
                buf[n] = '\0';
                std::string resp(buf, static_cast<size_t>(n));
                response_count.fetch_add(1);
                if (resp.find("200 OK") != std::string::npos &&
                    resp.find("\"status\":\"ok\"") != std::string::npos) {
                    success_count.fetch_add(1);
                }
            }
        });
    }

    for (auto& t : client_threads) {
        t.join();
    }

    real_server.stop();
    server_thread.join();

    EXPECT_EQ(response_count.load(), NUM_REQUESTS)
        << "All " << NUM_REQUESTS << " requests should get responses";
    EXPECT_EQ(success_count.load(), NUM_REQUESTS)
        << "All " << NUM_REQUESTS << " requests should get 200 OK with health status";
}

}  // namespace
}  // namespace ops
