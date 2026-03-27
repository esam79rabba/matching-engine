#include "transport/tls_socket.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

namespace transport {

// ─── TlsSocket ─────────────────────────────────────────────

TlsSocket::TlsSocket(int fd, SSL* ssl) : fd_(fd), ssl_(ssl) {}

TlsSocket::~TlsSocket() { close(); }

int TlsSocket::send(const void* data, size_t len) {
    if (!ssl_ || fd_ < 0) return -1;
    int n = SSL_write(ssl_, data, static_cast<int>(len));
    if (n <= 0) {
        int err = SSL_get_error(ssl_, n);
        if (err == SSL_ERROR_ZERO_RETURN) return 0;
        return -1;
    }
    return n;
}

int TlsSocket::recv(void* buf, size_t len) {
    if (!ssl_ || fd_ < 0) return -1;
    int n = SSL_read(ssl_, buf, static_cast<int>(len));
    if (n <= 0) {
        int err = SSL_get_error(ssl_, n);
        if (err == SSL_ERROR_ZERO_RETURN) return 0;
        return -1;
    }
    return n;
}

void TlsSocket::close() {
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool TlsSocket::is_open() const { return ssl_ != nullptr && fd_ >= 0; }

int TlsSocket::fd() const { return fd_; }


// ─── TlsServerSocket ──────────────────────────────────────

TlsServerSocket::TlsServerSocket(uint16_t port, const TlsConfig& config)
    : port_(port), config_(config) {}

TlsServerSocket::~TlsServerSocket() { close(); }

bool TlsServerSocket::init_context() {
    ctx_ = SSL_CTX_new(TLS_server_method());
    if (!ctx_) {
        fprintf(stderr, "[TLS] SSL_CTX_new failed\n");
        return false;
    }

    // Set minimum TLS version
    if (config_.min_tls_version == "TLSv1.2") {
        SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);
    } else if (config_.min_tls_version == "TLSv1.3") {
        SSL_CTX_set_min_proto_version(ctx_, TLS1_3_VERSION);
    }

    // Set cipher list if specified
    if (!config_.cipher_list.empty()) {
        if (SSL_CTX_set_cipher_list(ctx_, config_.cipher_list.c_str()) != 1) {
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
            return false;
        }
    }

    // Load server certificate
    if (SSL_CTX_use_certificate_file(ctx_, config_.cert_file.c_str(), SSL_FILETYPE_PEM) != 1) {
        fprintf(stderr, "[TLS] Failed to load certificate: %s\n", config_.cert_file.c_str());
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx_); ctx_ = nullptr;
        return false;
    }

    // Load server private key
    if (SSL_CTX_use_PrivateKey_file(ctx_, config_.key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
        fprintf(stderr, "[TLS] Failed to load private key: %s\n", config_.key_file.c_str());
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx_); ctx_ = nullptr;
        return false;
    }

    // Verify private key matches certificate
    if (SSL_CTX_check_private_key(ctx_) != 1) {
        fprintf(stderr, "[TLS] Private key does not match certificate\n");
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(ctx_); ctx_ = nullptr;
        return false;
    }

    // Configure client certificate verification (mTLS)
    if (config_.require_client_cert) {
        SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
        if (!config_.ca_file.empty()) {
            if (SSL_CTX_load_verify_locations(ctx_, config_.ca_file.c_str(), nullptr) != 1) {
                SSL_CTX_free(ctx_);
                ctx_ = nullptr;
                return false;
            }
        }
    }

    return true;
}

bool TlsServerSocket::listen() {
    if (!ctx_) return false;  // init_context() must be called first

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;

    int opt = 1;
    (void)setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (::listen(listen_fd_, 64) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    // Retrieve the actual bound port (important when port_ == 0)
    struct sockaddr_in bound{};
    socklen_t bound_len = sizeof(bound);
    if (getsockname(listen_fd_, reinterpret_cast<struct sockaddr*>(&bound), &bound_len) == 0) {
        bound_port_ = ntohs(bound.sin_port);
    } else {
        bound_port_ = port_;
    }

    return true;
}

ISocket* TlsServerSocket::accept() {
    if (listen_fd_ < 0 || !ctx_) return nullptr;

    struct sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = ::accept(listen_fd_,
                              reinterpret_cast<struct sockaddr*>(&client_addr),
                              &client_len);
    if (client_fd < 0) return nullptr;

    SSL* ssl = SSL_new(ctx_);
    if (!ssl) {
        ::close(client_fd);
        return nullptr;
    }

    SSL_set_fd(ssl, client_fd);

    if (SSL_accept(ssl) != 1) {
        SSL_free(ssl);
        ::close(client_fd);
        return nullptr;
    }

    return new TlsSocket(client_fd, ssl);
}

void TlsServerSocket::close() {
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (ctx_) {
        SSL_CTX_free(ctx_);
        ctx_ = nullptr;
    }
}

bool TlsServerSocket::is_listening() const { return listen_fd_ >= 0; }

uint16_t TlsServerSocket::port() const { return port_; }

int TlsServerSocket::fd() const { return listen_fd_; }

uint16_t TlsServerSocket::bound_port() const { return bound_port_; }

}  // namespace transport
