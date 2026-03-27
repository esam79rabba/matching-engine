#include "marketdata/transport/md_udp_publisher.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sstream>

namespace marketdata {

MdUdpPublisher::MdUdpPublisher(const Config& config)
    : config_(config) {}

MdUdpPublisher::~MdUdpPublisher() {
    stop();
}

bool MdUdpPublisher::start() {
    socket_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) return false;

    // Set multicast TTL
    int ttl = config_.ttl;
    if (::setsockopt(socket_fd_, IPPROTO_IP, IP_MULTICAST_TTL,
                     &ttl, sizeof(ttl)) < 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    // Set multicast interface
    struct in_addr local_iface{};
    local_iface.s_addr = ::inet_addr(config_.interface_addr.c_str());
    if (::setsockopt(socket_fd_, IPPROTO_IP, IP_MULTICAST_IF,
                     &local_iface, sizeof(local_iface)) < 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }

    return true;
}

void MdUdpPublisher::stop() {
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
}

void MdUdpPublisher::send_json(const std::string& json) {
    if (socket_fd_ < 0) return;

    struct sockaddr_in mcast_addr{};
    mcast_addr.sin_family = AF_INET;
    mcast_addr.sin_port = htons(config_.port);
    mcast_addr.sin_addr.s_addr = ::inet_addr(config_.multicast_group.c_str());

    std::string msg = json + "\n";
    ::sendto(socket_fd_, msg.c_str(), msg.size(), 0,
             reinterpret_cast<const struct sockaddr*>(&mcast_addr),
             sizeof(mcast_addr));
    ++messages_sent_;
}

void MdUdpPublisher::publish_top_of_book(const TopOfBook& tob) {
    ++sequence_;
    std::ostringstream oss;
    oss << "{\"type\":\"tob\",\"seq\":" << sequence_
        << ",\"inst\":" << tob.instrument_id
        << ",\"bid\":" << tob.best_bid
        << ",\"bid_qty\":" << tob.best_bid_qty
        << ",\"ask\":" << tob.best_ask
        << ",\"ask_qty\":" << tob.best_ask_qty
        << ",\"ts\":" << tob.timestamp << "}";
    send_json(oss.str());
}

void MdUdpPublisher::publish_depth_update(const DepthUpdate& update) {
    ++sequence_;
    std::ostringstream oss;
    oss << "{\"type\":\"depth\",\"seq\":" << sequence_
        << ",\"inst\":" << update.instrument_id
        << ",\"side\":" << static_cast<int>(update.side)
        << ",\"price\":" << update.price
        << ",\"qty\":" << update.new_qty
        << ",\"orders\":" << update.order_count
        << ",\"ts\":" << update.timestamp << "}";
    send_json(oss.str());
}

void MdUdpPublisher::publish_trade(const TradeMessage& trade) {
    ++sequence_;
    std::ostringstream oss;
    oss << "{\"type\":\"trade\",\"seq\":" << sequence_
        << ",\"inst\":" << trade.instrument_id
        << ",\"price\":" << trade.price
        << ",\"qty\":" << trade.quantity
        << ",\"aggressor\":" << static_cast<int>(trade.aggressor_side)
        << ",\"trade_id\":" << trade.trade_id
        << ",\"ts\":" << trade.timestamp << "}";
    send_json(oss.str());
}

void MdUdpPublisher::publish_instrument_status(const InstrumentStatus& status) {
    ++sequence_;
    std::ostringstream oss;
    oss << "{\"type\":\"status\",\"seq\":" << sequence_
        << ",\"inst\":" << status.instrument_id
        << ",\"state\":" << static_cast<int>(status.new_state)
        << ",\"ts\":" << status.timestamp << "}";
    send_json(oss.str());
}

}  // namespace marketdata
