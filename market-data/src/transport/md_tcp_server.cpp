#include "marketdata/transport/md_tcp_server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <sstream>
#include <algorithm>

// MSG_NOSIGNAL is Linux-only; macOS uses SO_NOSIGPIPE instead
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace marketdata {

MdTcpServer::MdTcpServer(uint16_t port) : port_(port) {}

MdTcpServer::~MdTcpServer() { stop(); }

bool MdTcpServer::start() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, 16) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    running_ = true;
    return true;
}

void MdTcpServer::stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (int fd : client_fds_) {
        if (fd >= 0) close(fd);
    }
    client_fds_.clear();
}

void MdTcpServer::accept_clients() {
    if (listen_fd_ < 0) return;

    struct timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 0;  // Non-blocking check
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(listen_fd_, &read_fds);

    if (select(listen_fd_ + 1, &read_fds, nullptr, nullptr, &tv) > 0) {
        int client_fd = accept(listen_fd_, nullptr, nullptr);
        if (client_fd >= 0) {
            // Set client socket to non-blocking mode so slow clients
            // cannot stall the broadcast loop.
            int flags = fcntl(client_fd, F_GETFL, 0);
            if (flags >= 0) {
                fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
            }
            std::lock_guard<std::mutex> lock(clients_mutex_);
            client_fds_.push_back(client_fd);
        }
    }
}

void MdTcpServer::broadcast(const std::string& json_line) {
    accept_clients();  // Check for new connections

    std::lock_guard<std::mutex> lock(clients_mutex_);

    std::string msg = json_line + "\n";
    std::vector<int> active;
    for (int fd : client_fds_) {
        ssize_t sent = ::send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
        if (sent > 0) {
            active.push_back(fd);
        } else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // Client's send buffer is full — skip this message, keep client
            active.push_back(fd);
            ++messages_dropped_;
        } else {
            // Send error (disconnected, broken pipe, etc.) — drop client
            close(fd);
        }
    }
    client_fds_ = std::move(active);
    ++messages_sent_;
}

void MdTcpServer::publish_top_of_book(const TopOfBook& tob) {
    ++sequence_;
    std::ostringstream oss;
    oss << "{\"type\":\"tob\",\"seq\":" << sequence_
        << ",\"inst\":" << tob.instrument_id
        << ",\"bid\":" << tob.best_bid
        << ",\"bid_qty\":" << tob.best_bid_qty
        << ",\"ask\":" << tob.best_ask
        << ",\"ask_qty\":" << tob.best_ask_qty
        << ",\"ts\":" << tob.timestamp << "}";
    broadcast(oss.str());
}

void MdTcpServer::publish_depth_update(const DepthUpdate& update) {
    ++sequence_;
    std::ostringstream oss;
    oss << "{\"type\":\"depth\",\"seq\":" << sequence_
        << ",\"inst\":" << update.instrument_id
        << ",\"side\":" << static_cast<int>(update.side)
        << ",\"price\":" << update.price
        << ",\"qty\":" << update.new_qty
        << ",\"orders\":" << update.order_count
        << ",\"ts\":" << update.timestamp << "}";
    broadcast(oss.str());
}

void MdTcpServer::publish_trade(const TradeMessage& trade) {
    ++sequence_;
    std::ostringstream oss;
    oss << "{\"type\":\"trade\",\"seq\":" << sequence_
        << ",\"inst\":" << trade.instrument_id
        << ",\"price\":" << trade.price
        << ",\"qty\":" << trade.quantity
        << ",\"aggressor\":" << static_cast<int>(trade.aggressor_side)
        << ",\"trade_id\":" << trade.trade_id
        << ",\"ts\":" << trade.timestamp << "}";
    broadcast(oss.str());
}

void MdTcpServer::publish_instrument_status(const InstrumentStatus& status) {
    ++sequence_;
    std::ostringstream oss;
    oss << "{\"type\":\"status\",\"seq\":" << sequence_
        << ",\"inst\":" << status.instrument_id
        << ",\"state\":" << static_cast<int>(status.new_state)
        << ",\"ts\":" << status.timestamp << "}";
    broadcast(oss.str());
}

uint32_t MdTcpServer::client_count() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return static_cast<uint32_t>(client_fds_.size());
}

}  // namespace marketdata
