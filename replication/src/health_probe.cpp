#include "replication/health_probe.h"

#include <cstring>
#include <string>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>

namespace replication {

// ─── Constructor / Destructor ────────────────────────────────

HealthProbe::HealthProbe(const Config& config)
    : config_(config) {}

HealthProbe::~HealthProbe() {
    stop();
}

// ─── Start / Stop ────────────────────────────────────────────

void HealthProbe::start() {
    if (running_.load()) return;
    running_.store(true);
    probe_thread_ = std::thread([this]() { probe_loop(); });
}

void HealthProbe::stop() {
    running_.store(false);
    if (probe_thread_.joinable()) {
        probe_thread_.join();
    }
}

// ─── Probe loop ──────────────────────────────────────────────

void HealthProbe::probe_loop() {
    while (running_.load()) {
        bool healthy = check_health();
        State old_state = state_.load();
        State new_state = old_state;

        if (healthy) {
            consecutive_failures_.store(0);
            new_state = State::HEALTHY;
        } else {
            int failures = consecutive_failures_.fetch_add(1) + 1;
            if (failures >= config_.failure_threshold) {
                new_state = State::FAILED;
            } else {
                new_state = State::DEGRADED;
            }
        }

        if (new_state != old_state) {
            state_.store(new_state);
            if (state_callback_) {
                state_callback_(old_state, new_state);
            }
        }

        // Sleep for interval, but check running_ periodically to allow quick shutdown
        int remaining_ms = config_.interval_ms;
        while (remaining_ms > 0 && running_.load()) {
            int sleep_ms = std::min(remaining_ms, 50);
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
            remaining_ms -= sleep_ms;
        }
    }
}

// ─── Health check ────────────────────────────────────────────

bool HealthProbe::check_health() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    // Set connect/send/recv timeout
    struct timeval tv{};
    tv.tv_sec = config_.timeout_ms / 1000;
    tv.tv_usec = (config_.timeout_ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.primary_port);
    ::inet_pton(AF_INET, config_.primary_host.c_str(), &addr.sin_addr);

    // Attempt connection
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return false;
    }

    // Send minimal HTTP GET request
    std::string request = "GET " + config_.health_path + " HTTP/1.0\r\n"
                          "Host: " + config_.primary_host + "\r\n"
                          "\r\n";

    ssize_t sent = ::send(fd, request.c_str(), request.size(), 0);
    if (sent <= 0) {
        ::close(fd);
        return false;
    }

    // Read response — check for "200" in status line
    char buf[512];
    std::memset(buf, 0, sizeof(buf));

    // Poll for data with timeout
    struct pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    int ret = ::poll(&pfd, 1, config_.timeout_ms);
    if (ret <= 0) {
        ::close(fd);
        return false;
    }

    ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
    ::close(fd);

    if (n <= 0) return false;

    // Check for HTTP 200 response
    std::string response(buf, static_cast<size_t>(n));
    return response.find("200") != std::string::npos;
}

}  // namespace replication
