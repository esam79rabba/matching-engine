#pragma once
#include "transport/socket.h"
#include <string>

// Forward declare OpenSSL types to avoid header pollution
typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;

namespace transport {

/// TLS-wrapped socket — implements ISocket over OpenSSL.
/// Wraps an existing connected TCP file descriptor with TLS.
class TlsSocket : public ISocket {
public:
    /// Wrap an fd with a TLS connection using the provided SSL object.
    /// Takes ownership of the SSL object.
    TlsSocket(int fd, SSL* ssl);
    ~TlsSocket() override;

    TlsSocket(const TlsSocket&) = delete;
    TlsSocket& operator=(const TlsSocket&) = delete;

    int send(const void* data, size_t len) override;
    int recv(void* buf, size_t len) override;
    void close() override;
    [[nodiscard]] bool is_open() const override;
    [[nodiscard]] int fd() const override;

private:
    int fd_;
    SSL* ssl_;
};

/// TLS server socket — accepts connections and performs TLS handshake.
class TlsServerSocket : public IServerSocket {
public:
    /// Create with port and TLS config. Call init_context() before listen().
    TlsServerSocket(uint16_t port, const TlsConfig& config);
    ~TlsServerSocket() override;

    TlsServerSocket(const TlsServerSocket&) = delete;
    TlsServerSocket& operator=(const TlsServerSocket&) = delete;

    /// Initialize OpenSSL context with certificates. Must be called before listen().
    bool init_context();

    bool listen() override;
    ISocket* accept() override;  // Returns TlsSocket* after handshake
    void close() override;
    [[nodiscard]] bool is_listening() const override;
    [[nodiscard]] uint16_t port() const override;
    [[nodiscard]] int fd() const override;
    [[nodiscard]] uint16_t bound_port() const;

private:
    uint16_t port_;
    uint16_t bound_port_ = 0;
    int listen_fd_ = -1;
    TlsConfig config_;
    SSL_CTX* ctx_ = nullptr;
};

}  // namespace transport
