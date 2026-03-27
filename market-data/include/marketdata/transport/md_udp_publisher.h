#pragma once

#include "marketdata/publisher.h"
#include <string>
#include <cstdint>
#include <atomic>

struct sockaddr_in;

namespace marketdata {

/// UDP multicast market data publisher.
/// Sends JSON-encoded events to a multicast group.
/// Foundation implementation — SBE encoding deferred.
class MdUdpPublisher : public IPublisherSink {
public:
    struct Config {
        std::string multicast_group = "239.1.1.1";
        uint16_t port = 9878;
        std::string interface_addr = "0.0.0.0";
        uint8_t ttl = 1;
    };

    explicit MdUdpPublisher(const Config& config);
    ~MdUdpPublisher();

    /// Create and configure the UDP socket for multicast sending.
    bool start();

    /// Close the socket.
    void stop();

    // IPublisherSink
    void publish_top_of_book(const TopOfBook& tob) override;
    void publish_depth_update(const DepthUpdate& update) override;
    void publish_trade(const TradeMessage& trade) override;
    void publish_instrument_status(const InstrumentStatus& status) override;

    [[nodiscard]] uint64_t messages_sent() const { return messages_sent_.load(); }
    [[nodiscard]] uint64_t sequence() const { return sequence_; }
    [[nodiscard]] bool is_running() const { return socket_fd_ >= 0; }

private:
    Config config_;
    int socket_fd_ = -1;
    std::atomic<uint64_t> messages_sent_{0};
    uint64_t sequence_ = 0;

    void send_json(const std::string& json);
};

}  // namespace marketdata
