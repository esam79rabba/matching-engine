#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace transport {

/// Abstract socket interface — transport-agnostic read/write.
///
/// Implementations:
///   - TcpSocket: plain POSIX TCP (current FR-1 prototype)
///   - TlsSocket: OpenSSL-backed TLS (Phase 2+)
///
/// All TCP servers (FIX acceptor, market data, ops HTTP) can adopt
/// this interface to make TLS a zero-change swap at the application layer.
///
/// Generic — no protocol or asset-class logic.
class ISocket {
public:
    virtual ~ISocket() = default;

    /// Send data. Returns bytes sent, or -1 on error.
    virtual int send(const void* data, size_t len) = 0;

    /// Receive data. Returns bytes received, 0 on close, or -1 on error.
    virtual int recv(void* buf, size_t len) = 0;

    /// Close the socket.
    virtual void close() = 0;

    /// Check if the socket is open and usable.
    [[nodiscard]] virtual bool is_open() const = 0;

    /// Get the underlying file descriptor (for select/poll/epoll).
    /// Returns -1 if not applicable (e.g., some TLS wrappers).
    [[nodiscard]] virtual int fd() const = 0;
};


/// Abstract server socket — accepts incoming connections.
///
/// Wraps the listen/accept cycle. TLS implementation performs
/// the handshake inside accept() before returning an ISocket.
class IServerSocket {
public:
    virtual ~IServerSocket() = default;

    /// Bind and listen on the configured address/port.
    virtual bool listen() = 0;

    /// Accept a client connection. Blocks until a client connects.
    /// Returns nullptr on error or shutdown.
    /// The caller owns the returned socket.
    virtual ISocket* accept() = 0;

    /// Stop listening and close the server socket.
    virtual void close() = 0;

    [[nodiscard]] virtual bool is_listening() const = 0;
    [[nodiscard]] virtual uint16_t port() const = 0;

    /// Get the underlying listen file descriptor (for select/poll/epoll).
    /// Returns -1 if not listening.
    [[nodiscard]] virtual int fd() const = 0;
};


/// TLS configuration — used by TLS socket implementations.
///
/// Phase 2+ production will use these fields to configure OpenSSL/BoringSSL.
/// FR-1 prototype includes the struct for API stability.
struct TlsConfig {
    std::string cert_file;          // Server certificate (PEM)
    std::string key_file;           // Server private key (PEM)
    std::string ca_file;            // CA certificate for client verification (mTLS)
    bool        require_client_cert = false;  // Enable mTLS
    std::string min_tls_version = "TLSv1.2"; // Minimum protocol version
    std::string cipher_list;        // Allowed cipher suites (empty = default)
};

}  // namespace transport
