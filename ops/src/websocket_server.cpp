#include "ops/websocket_server.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <iostream>
#include <algorithm>

// SHA-1 for WebSocket handshake (minimal implementation)
// In production, use OpenSSL EVP. This is a self-contained fallback.
namespace {

// Base64 encoding table
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const uint8_t* data, size_t len) {
    std::string result;
    result.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
        result += b64[(n >> 18) & 0x3F];
        result += b64[(n >> 12) & 0x3F];
        result += (i + 1 < len) ? b64[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? b64[n & 0x3F] : '=';
    }
    return result;
}

// Minimal SHA-1 for WebSocket accept key
struct SHA1 {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint8_t buffer[64]{};
    uint64_t total_bits = 0;
    size_t buf_len = 0;

    static uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

    void process_block(const uint8_t* block) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = (static_cast<uint32_t>(block[i*4]) << 24) | (static_cast<uint32_t>(block[i*4+1]) << 16)
                  | (static_cast<uint32_t>(block[i*4+2]) << 8) | block[i*4+3];
        for (int i = 16; i < 80; i++)
            w[i] = rotl(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

        uint32_t a = h[0], b_ = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20)      { f = (b_ & c) | (~b_ & d); k = 0x5A827999; }
            else if (i < 40) { f = b_ ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b_ & c) | (b_ & d) | (c & d); k = 0x8F1BBCDC; }
            else              { f = b_ ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t tmp = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b_, 30); b_ = a; a = tmp;
        }
        h[0] += a; h[1] += b_; h[2] += c; h[3] += d; h[4] += e;
    }

    void update(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; i++) {
            buffer[buf_len++] = data[i];
            total_bits += 8;
            if (buf_len == 64) { process_block(buffer); buf_len = 0; }
        }
    }

    void update(const std::string& s) { update(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }

    std::string digest() {
        buffer[buf_len++] = 0x80;
        while (buf_len != 56) {
            if (buf_len == 64) { process_block(buffer); buf_len = 0; }
            buffer[buf_len++] = 0;
        }
        for (int i = 7; i >= 0; i--) buffer[buf_len++] = static_cast<uint8_t>(total_bits >> (i * 8));
        process_block(buffer);

        uint8_t result[20];
        for (int i = 0; i < 5; i++) {
            result[i*4]   = static_cast<uint8_t>(h[i] >> 24);
            result[i*4+1] = static_cast<uint8_t>(h[i] >> 16);
            result[i*4+2] = static_cast<uint8_t>(h[i] >> 8);
            result[i*4+3] = static_cast<uint8_t>(h[i]);
        }
        return base64_encode(result, 20);
    }
};

}  // anonymous namespace

namespace ops {

WebSocketServer::WebSocketServer(uint16_t port) : port_(port) {}

WebSocketServer::~WebSocketServer() { stop(); }

bool WebSocketServer::start() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(listen_fd_); listen_fd_ = -1; return false;
    }
    if (::listen(listen_fd_, 16) < 0) {
        ::close(listen_fd_); listen_fd_ = -1; return false;
    }

    running_ = true;

    while (running_) {
        struct timeval tv{}; tv.tv_sec = 0; tv.tv_usec = 100000;
        fd_set read_fds; FD_ZERO(&read_fds); FD_SET(listen_fd_, &read_fds);
        int ready = select(listen_fd_ + 1, &read_fds, nullptr, nullptr, &tv);
        if (ready <= 0) continue;

        int client_fd = ::accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0) continue;

        active_clients_.fetch_add(1);
        std::thread([this, client_fd]() {
            handle_client(client_fd);
            remove_client(client_fd);
            ::close(client_fd);
            active_clients_.fetch_sub(1);
        }).detach();
    }
    return true;
}

void WebSocketServer::stop() {
    running_ = false;
    if (listen_fd_ >= 0) { ::close(listen_fd_); listen_fd_ = -1; }

    // Close all client connections
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (int fd : client_fds_) ::close(fd);
    client_fds_.clear();
    subscriptions_.clear();

    int wait_ms = 0;
    while (active_clients_.load() > 0 && wait_ms < 3000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        wait_ms += 50;
    }
}

void WebSocketServer::broadcast(const std::string& channel, const std::string& json_message) {
    std::string frame = encode_frame(json_message);
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& [fd, subs] : subscriptions_) {
        if (subs.count(channel) > 0 || subs.count("*") > 0) {
            ::send(fd, frame.c_str(), frame.size(), MSG_NOSIGNAL);
        }
    }
}

void WebSocketServer::send_to(int client_fd, const std::string& message) {
    std::string frame = encode_frame(message);
    ::send(client_fd, frame.c_str(), frame.size(), MSG_NOSIGNAL);
}

uint32_t WebSocketServer::client_count() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return static_cast<uint32_t>(client_fds_.size());
}

void WebSocketServer::handle_client(int client_fd) {
    // Read HTTP upgrade request
    char buf[4096];
    ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return;
    buf[n] = '\0';
    std::string request(buf, static_cast<size_t>(n));

    // Perform WebSocket handshake
    if (!perform_handshake(client_fd, request)) return;

    // Register client
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        client_fds_.insert(client_fd);
        subscriptions_[client_fd] = {"*"};  // Subscribe to all by default
    }

    // Read frames
    while (running_) {
        struct timeval tv{}; tv.tv_sec = 1; tv.tv_usec = 0;
        fd_set read_fds; FD_ZERO(&read_fds); FD_SET(client_fd, &read_fds);
        int ready = select(client_fd + 1, &read_fds, nullptr, nullptr, &tv);
        if (ready <= 0) continue;

        uint8_t frame_buf[4096];
        ssize_t received = recv(client_fd, frame_buf, sizeof(frame_buf), 0);
        if (received <= 0) break;  // Client disconnected

        size_t consumed = 0;
        std::string payload = decode_frame(frame_buf, static_cast<size_t>(received), consumed);

        if (payload.empty()) {
            // Check for close frame (opcode 0x8)
            if (received >= 2 && (frame_buf[0] & 0x0F) == 0x8) break;
            continue;
        }

        process_frame(client_fd, payload);
    }
}

bool WebSocketServer::perform_handshake(int client_fd, const std::string& request) {
    // Find Sec-WebSocket-Key header
    auto key_pos = request.find("Sec-WebSocket-Key: ");
    if (key_pos == std::string::npos) return false;
    key_pos += 19;
    auto key_end = request.find("\r\n", key_pos);
    if (key_end == std::string::npos) return false;
    std::string client_key = request.substr(key_pos, key_end - key_pos);

    std::string accept_key = compute_accept_key(client_key);

    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << accept_key << "\r\n"
             << "\r\n";

    std::string resp = response.str();
    ::send(client_fd, resp.c_str(), resp.size(), 0);
    return true;
}

void WebSocketServer::process_frame(int client_fd, const std::string& payload) {
    // Expect JSON: {"action":"subscribe","channel":"trades"}
    // or: {"action":"unsubscribe","channel":"trades"}
    // Simple parsing without JSON library
    if (payload.find("\"subscribe\"") != std::string::npos) {
        auto ch_pos = payload.find("\"channel\"");
        if (ch_pos != std::string::npos) {
            auto q1 = payload.find('"', payload.find(':', ch_pos) + 1);
            auto q2 = payload.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) {
                std::string channel = payload.substr(q1 + 1, q2 - q1 - 1);
                std::lock_guard<std::mutex> lock(clients_mutex_);
                subscriptions_[client_fd].insert(channel);
            }
        }
    } else if (payload.find("\"unsubscribe\"") != std::string::npos) {
        auto ch_pos = payload.find("\"channel\"");
        if (ch_pos != std::string::npos) {
            auto q1 = payload.find('"', payload.find(':', ch_pos) + 1);
            auto q2 = payload.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) {
                std::string channel = payload.substr(q1 + 1, q2 - q1 - 1);
                std::lock_guard<std::mutex> lock(clients_mutex_);
                subscriptions_[client_fd].erase(channel);
            }
        }
    }
}

void WebSocketServer::remove_client(int client_fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    client_fds_.erase(client_fd);
    subscriptions_.erase(client_fd);
}

// WebSocket frame encoding (RFC 6455)
std::string WebSocketServer::encode_frame(const std::string& payload) {
    std::string frame;
    frame += static_cast<char>(0x81);  // FIN + text opcode

    if (payload.size() < 126) {
        frame += static_cast<char>(payload.size());
    } else if (payload.size() < 65536) {
        frame += static_cast<char>(126);
        frame += static_cast<char>((payload.size() >> 8) & 0xFF);
        frame += static_cast<char>(payload.size() & 0xFF);
    } else {
        frame += static_cast<char>(127);
        for (int i = 7; i >= 0; i--)
            frame += static_cast<char>((payload.size() >> (i * 8)) & 0xFF);
    }

    frame += payload;
    return frame;
}

// WebSocket frame decoding (client→server frames are masked per RFC 6455)
std::string WebSocketServer::decode_frame(const uint8_t* data, size_t len, size_t& consumed) {
    consumed = 0;
    if (len < 2) return "";

    uint8_t opcode = data[0] & 0x0F;
    bool masked = (data[1] & 0x80) != 0;
    uint64_t payload_len = data[1] & 0x7F;
    size_t offset = 2;

    if (payload_len == 126) {
        if (len < 4) return "";
        payload_len = (static_cast<uint64_t>(data[2]) << 8) | data[3];
        offset = 4;
    } else if (payload_len == 127) {
        if (len < 10) return "";
        payload_len = 0;
        for (int i = 0; i < 8; i++)
            payload_len = (payload_len << 8) | data[2 + i];
        offset = 10;
    }

    uint8_t mask[4] = {};
    if (masked) {
        if (len < offset + 4) return "";
        memcpy(mask, data + offset, 4);
        offset += 4;
    }

    if (len < offset + payload_len) return "";

    std::string payload(payload_len, '\0');
    for (uint64_t i = 0; i < payload_len; i++) {
        payload[i] = static_cast<char>(data[offset + i] ^ (masked ? mask[i % 4] : 0));
    }

    consumed = offset + payload_len;

    // Only handle text frames (opcode 1)
    if (opcode == 0x1) return payload;
    return "";
}

std::string WebSocketServer::compute_accept_key(const std::string& client_key) {
    // RFC 6455: concatenate with magic GUID, SHA-1, base64
    SHA1 sha;
    sha.update(client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    return sha.digest();
}

}  // namespace ops
