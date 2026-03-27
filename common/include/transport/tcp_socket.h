#pragma once

#include "transport/socket.h"
#include <cstdint>

namespace transport {

/// Plain TCP socket — wraps a POSIX file descriptor.
///
/// This is the FR-1 transport implementation. All TCP servers
/// (FIX acceptor, market data, ops HTTP) can use this today,
/// then swap to TlsSocket (Phase 2+) without changing application code.
class TcpSocket : public ISocket {
public:
    /// Wrap an existing connected socket fd (from accept()).
    explicit TcpSocket(int fd);
    ~TcpSocket() override;

    // Non-copyable, movable
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;
    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;

    int send(const void* data, size_t len) override;
    int recv(void* buf, size_t len) override;
    void close() override;
    [[nodiscard]] bool is_open() const override { return fd_ >= 0; }
    [[nodiscard]] int fd() const override { return fd_; }

private:
    int fd_;
};


/// Plain TCP server socket — bind/listen/accept cycle.
class TcpServerSocket : public IServerSocket {
public:
    explicit TcpServerSocket(uint16_t port);
    ~TcpServerSocket() override;

    // Non-copyable
    TcpServerSocket(const TcpServerSocket&) = delete;
    TcpServerSocket& operator=(const TcpServerSocket&) = delete;

    bool listen() override;
    ISocket* accept() override;
    void close() override;
    [[nodiscard]] bool is_listening() const override { return listen_fd_ >= 0; }
    [[nodiscard]] uint16_t port() const override { return port_; }
    [[nodiscard]] int fd() const override { return listen_fd_; }

    /// Get the actual bound port (useful if constructed with port 0).
    [[nodiscard]] uint16_t bound_port() const { return bound_port_; }

private:
    uint16_t port_;
    uint16_t bound_port_ = 0;
    int listen_fd_ = -1;
};

}  // namespace transport
