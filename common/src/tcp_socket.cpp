#include "transport/tcp_socket.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

// MSG_NOSIGNAL is Linux-only; macOS uses SO_NOSIGPIPE instead
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace transport {

// ─── TcpSocket ─────────────────────────────────────────────

TcpSocket::TcpSocket(int fd) : fd_(fd) {}

TcpSocket::~TcpSocket() { close(); }

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

int TcpSocket::send(const void* data, size_t len) {
    if (fd_ < 0) return -1;
    return static_cast<int>(::send(fd_, data, len, MSG_NOSIGNAL));
}

int TcpSocket::recv(void* buf, size_t len) {
    if (fd_ < 0) return -1;
    return static_cast<int>(::recv(fd_, buf, len, 0));
}

void TcpSocket::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}


// ─── TcpServerSocket ──────────────────────────────────────

TcpServerSocket::TcpServerSocket(uint16_t port) : port_(port) {}

TcpServerSocket::~TcpServerSocket() { close(); }

bool TcpServerSocket::listen() {
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

ISocket* TcpServerSocket::accept() {
    if (listen_fd_ < 0) return nullptr;

    struct sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = ::accept(listen_fd_,
                              reinterpret_cast<struct sockaddr*>(&client_addr),
                              &client_len);
    if (client_fd < 0) return nullptr;

    return new TcpSocket(client_fd);
}

void TcpServerSocket::close() {
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

}  // namespace transport
