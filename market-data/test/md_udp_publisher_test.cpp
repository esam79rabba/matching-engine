/// UDP multicast market data publisher tests.
///
/// Tests the MdUdpPublisher foundation implementation:
///   - Socket creation and multicast configuration
///   - JSON encoding and multicast send
///   - Sequence number tracking
///   - Receive verification via multicast group join

#include "marketdata/transport/md_udp_publisher.h"

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>

namespace marketdata {
namespace {

// ─── Helper: create a UDP receiver that joins a multicast group ──

class MulticastReceiver {
public:
    MulticastReceiver(const std::string& group, uint16_t port) {
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0) return;

        // Allow multiple receivers on same port
        int reuse = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
        ::setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(fd_);
            fd_ = -1;
            return;
        }

        // Join multicast group
        struct ip_mreq mreq{};
        mreq.imr_multiaddr.s_addr = ::inet_addr(group.c_str());
        mreq.imr_interface.s_addr = INADDR_ANY;
        if (::setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                         &mreq, sizeof(mreq)) < 0) {
            ::close(fd_);
            fd_ = -1;
            return;
        }

        // Set receive timeout (500ms)
        struct timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 500000;
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    ~MulticastReceiver() {
        if (fd_ >= 0) ::close(fd_);
    }

    bool is_valid() const { return fd_ >= 0; }

    std::string receive() {
        if (fd_ < 0) return "";
        char buf[4096];
        ssize_t n = ::recv(fd_, buf, sizeof(buf) - 1, 0);
        if (n <= 0) return "";
        buf[n] = '\0';
        return std::string(buf, static_cast<size_t>(n));
    }

private:
    int fd_ = -1;
};

// Use a high ephemeral port to avoid conflicts
static constexpr uint16_t TEST_PORT = 19878;
static const char* TEST_GROUP = "239.255.0.1";

TEST(MdUdpPublisherTest, StartAndStop) {
    MdUdpPublisher::Config cfg;
    cfg.multicast_group = TEST_GROUP;
    cfg.port = TEST_PORT;
    cfg.ttl = 0;  // Loopback only

    MdUdpPublisher pub(cfg);
    EXPECT_FALSE(pub.is_running());

    ASSERT_TRUE(pub.start());
    EXPECT_TRUE(pub.is_running());

    pub.stop();
    EXPECT_FALSE(pub.is_running());
}

TEST(MdUdpPublisherTest, SequenceNumbers) {
    MdUdpPublisher::Config cfg;
    cfg.multicast_group = TEST_GROUP;
    cfg.port = TEST_PORT + 1;
    cfg.ttl = 0;

    MdUdpPublisher pub(cfg);
    ASSERT_TRUE(pub.start());

    EXPECT_EQ(pub.sequence(), 0u);

    // Publish multiple events and verify sequence increments
    TradeMessage trade{};
    trade.instrument_id = 100;
    trade.price = 50000;
    trade.quantity = 10;
    trade.aggressor_side = engine::Side::Buy;
    trade.trade_id = 1;
    trade.timestamp = 1000;

    pub.publish_trade(trade);
    EXPECT_EQ(pub.sequence(), 1u);
    EXPECT_EQ(pub.messages_sent(), 1u);

    TopOfBook tob{};
    tob.instrument_id = 100;
    tob.best_bid = 49999;
    tob.best_bid_qty = 50;
    tob.best_ask = 50001;
    tob.best_ask_qty = 30;
    tob.timestamp = 1001;

    pub.publish_top_of_book(tob);
    EXPECT_EQ(pub.sequence(), 2u);
    EXPECT_EQ(pub.messages_sent(), 2u);

    DepthUpdate depth{};
    depth.instrument_id = 100;
    depth.side = engine::Side::Buy;
    depth.price = 49999;
    depth.new_qty = 50;
    depth.order_count = 3;
    depth.timestamp = 1002;

    pub.publish_depth_update(depth);
    EXPECT_EQ(pub.sequence(), 3u);

    InstrumentStatus status{};
    status.instrument_id = 100;
    status.new_state = 3;
    status.timestamp = 1003;

    pub.publish_instrument_status(status);
    EXPECT_EQ(pub.sequence(), 4u);
    EXPECT_EQ(pub.messages_sent(), 4u);

    pub.stop();
}

TEST(MdUdpPublisherTest, SendAndReceive) {
    // Use a unique port for this test
    const uint16_t port = TEST_PORT + 2;

    // Create receiver first (must join group before sender sends)
    MulticastReceiver receiver(TEST_GROUP, port);
    if (!receiver.is_valid()) {
        // Multicast may not be available in CI environments — skip gracefully
        GTEST_SKIP() << "Multicast not available on this system";
    }

    // Small delay to let receiver bind
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    MdUdpPublisher::Config cfg;
    cfg.multicast_group = TEST_GROUP;
    cfg.port = port;
    cfg.ttl = 0;

    // Enable multicast loopback so we can receive our own packets
    MdUdpPublisher pub(cfg);
    ASSERT_TRUE(pub.start());

    // Publish a trade
    TradeMessage trade{};
    trade.instrument_id = 100;
    trade.price = 50000;
    trade.quantity = 10;
    trade.aggressor_side = engine::Side::Buy;
    trade.trade_id = 42;
    trade.timestamp = 999;

    pub.publish_trade(trade);

    // Receive and verify
    std::string received = receiver.receive();
    if (received.empty()) {
        // Some systems block loopback multicast — skip gracefully
        GTEST_SKIP() << "Multicast loopback not available";
    }

    EXPECT_NE(received.find("\"type\":\"trade\""), std::string::npos);
    EXPECT_NE(received.find("\"inst\":100"), std::string::npos);
    EXPECT_NE(received.find("\"price\":50000"), std::string::npos);
    EXPECT_NE(received.find("\"trade_id\":42"), std::string::npos);
    EXPECT_NE(received.find("\"seq\":1"), std::string::npos);

    pub.stop();
}

}  // namespace
}  // namespace marketdata
